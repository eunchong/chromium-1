// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/v8_inspector/V8DebuggerAgentImpl.h"

#include "platform/inspector_protocol/String16.h"
#include "platform/inspector_protocol/Values.h"
#include "platform/v8_inspector/InjectedScript.h"
#include "platform/v8_inspector/InspectedContext.h"
#include "platform/v8_inspector/JavaScriptCallFrame.h"
#include "platform/v8_inspector/RemoteObjectId.h"
#include "platform/v8_inspector/ScriptBreakpoint.h"
#include "platform/v8_inspector/V8InspectorSessionImpl.h"
#include "platform/v8_inspector/V8Regex.h"
#include "platform/v8_inspector/V8RuntimeAgentImpl.h"
#include "platform/v8_inspector/V8StackTraceImpl.h"
#include "platform/v8_inspector/V8StringUtil.h"
#include "platform/v8_inspector/public/V8ContentSearchUtil.h"
#include "platform/v8_inspector/public/V8Debugger.h"
#include "platform/v8_inspector/public/V8DebuggerClient.h"
#include "platform/v8_inspector/public/V8ToProtocolValue.h"

using blink::protocol::Array;
using blink::protocol::Maybe;
using blink::protocol::Debugger::BreakpointId;
using blink::protocol::Debugger::CallFrame;
using blink::protocol::Debugger::CollectionEntry;
using blink::protocol::Runtime::ExceptionDetails;
using blink::protocol::Debugger::FunctionDetails;
using blink::protocol::Debugger::GeneratorObjectDetails;
using blink::protocol::Runtime::ScriptId;
using blink::protocol::Runtime::StackTrace;
using blink::protocol::Runtime::RemoteObject;

namespace {
static const char v8AsyncTaskEventEnqueue[] = "enqueue";
static const char v8AsyncTaskEventWillHandle[] = "willHandle";
static const char v8AsyncTaskEventDidHandle[] = "didHandle";
}

namespace blink {

namespace DebuggerAgentState {
static const char javaScriptBreakpoints[] = "javaScriptBreakopints";
static const char pauseOnExceptionsState[] = "pauseOnExceptionsState";
static const char asyncCallStackDepth[] = "asyncCallStackDepth";
static const char blackboxPattern[] = "blackboxPattern";
static const char debuggerEnabled[] = "debuggerEnabled";

// Breakpoint properties.
static const char url[] = "url";
static const char isRegex[] = "isRegex";
static const char lineNumber[] = "lineNumber";
static const char columnNumber[] = "columnNumber";
static const char condition[] = "condition";
static const char skipAllPauses[] = "skipAllPauses";

} // namespace DebuggerAgentState;

static const int maxSkipStepFrameCount = 128;

static String16 breakpointIdSuffix(V8DebuggerAgentImpl::BreakpointSource source)
{
    switch (source) {
    case V8DebuggerAgentImpl::UserBreakpointSource:
        break;
    case V8DebuggerAgentImpl::DebugCommandBreakpointSource:
        return ":debug";
    case V8DebuggerAgentImpl::MonitorCommandBreakpointSource:
        return ":monitor";
    }
    return String16();
}

static String16 generateBreakpointId(const String16& scriptId, int lineNumber, int columnNumber, V8DebuggerAgentImpl::BreakpointSource source)
{
    return scriptId + ":" + String16::number(lineNumber) + ":" + String16::number(columnNumber) + breakpointIdSuffix(source);
}

static bool positionComparator(const std::pair<int, int>& a, const std::pair<int, int>& b)
{
    if (a.first != b.first)
        return a.first < b.first;
    return a.second < b.second;
}

static const LChar hexDigits[17] = "0123456789ABCDEF";

static void appendUnsignedAsHex(unsigned number, String16Builder* destination)
{
    for (size_t i = 0; i < 8; ++i) {
        destination->append(hexDigits[number & 0xF]);
        number >>= 4;
    }
}

// Hash algorithm for substrings is described in "Über die Komplexität der Multiplikation in
// eingeschränkten Branchingprogrammmodellen" by Woelfe.
// http://opendatastructures.org/versions/edition-0.1d/ods-java/node33.html#SECTION00832000000000000000
static String16 calculateHash(const String16& str)
{
    static uint64_t prime[] = { 0x3FB75161, 0xAB1F4E4F, 0x82675BC5, 0xCD924D35, 0x81ABE279 };
    static uint64_t random[] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    static uint32_t randomOdd[] = { 0xB4663807, 0xCC322BF5, 0xD4F91BBD, 0xA7BEA11D, 0x8F462907 };

    uint64_t hashes[] = { 0, 0, 0, 0, 0 };
    uint64_t zi[] = { 1, 1, 1, 1, 1 };

    const size_t hashesSize = WTF_ARRAY_LENGTH(hashes);

    size_t current = 0;
    const uint32_t* data = nullptr;
    data = reinterpret_cast<const uint32_t*>(str.characters16());
    for (size_t i = 0; i < str.sizeInBytes() / 4; i += 4) {
        uint32_t v = data[i];
        uint64_t xi = v * randomOdd[current] & 0x7FFFFFFF;
        hashes[current] = (hashes[current] + zi[current] * xi) % prime[current];
        zi[current] = (zi[current] * random[current]) % prime[current];
        current = current == hashesSize - 1 ? 0 : current + 1;
    }
    if (str.sizeInBytes() % 4) {
        uint32_t v = 0;
        for (size_t i = str.sizeInBytes() - str.sizeInBytes() % 4; i < str.sizeInBytes(); ++i) {
            v <<= 8;
            v |= reinterpret_cast<const uint8_t*>(data)[i];
        }
        uint64_t xi = v * randomOdd[current] & 0x7FFFFFFF;
        hashes[current] = (hashes[current] + zi[current] * xi) % prime[current];
        zi[current] = (zi[current] * random[current]) % prime[current];
        current = current == hashesSize - 1 ? 0 : current + 1;
    }

    for (size_t i = 0; i < hashesSize; ++i)
        hashes[i] = (hashes[i] + zi[i] * (prime[i] - 1)) % prime[i];

    String16Builder hash;
    for (size_t i = 0; i < hashesSize; ++i)
        appendUnsignedAsHex(hashes[i], &hash);
    return hash.toString();
}

static bool hasInternalError(ErrorString* errorString, bool hasError)
{
    if (hasError)
        *errorString = "Internal error";
    return hasError;
}

static PassOwnPtr<protocol::Debugger::Location> buildProtocolLocation(const String16& scriptId, int lineNumber, int columnNumber)
{
    return protocol::Debugger::Location::create()
        .setScriptId(scriptId)
        .setLineNumber(lineNumber)
        .setColumnNumber(columnNumber).build();
}

V8DebuggerAgentImpl::V8DebuggerAgentImpl(V8InspectorSessionImpl* session)
    : m_debugger(session->debugger())
    , m_session(session)
    , m_enabled(false)
    , m_state(nullptr)
    , m_frontend(nullptr)
    , m_isolate(m_debugger->isolate())
    , m_breakReason(protocol::Debugger::Paused::ReasonEnum::Other)
    , m_scheduledDebuggerStep(NoStep)
    , m_skipNextDebuggerStepOut(false)
    , m_javaScriptPauseScheduled(false)
    , m_steppingFromFramework(false)
    , m_pausingOnNativeEvent(false)
    , m_skippedStepFrameCount(0)
    , m_recursionLevelForStepOut(0)
    , m_recursionLevelForStepFrame(0)
    , m_skipAllPauses(false)
    , m_maxAsyncCallStackDepth(0)
{
    clearBreakDetails();
}

V8DebuggerAgentImpl::~V8DebuggerAgentImpl()
{
}

bool V8DebuggerAgentImpl::checkEnabled(ErrorString* errorString)
{
    if (enabled())
        return true;
    *errorString = "Debugger agent is not enabled";
    return false;
}

void V8DebuggerAgentImpl::enable()
{
    // debugger().addListener may result in reporting all parsed scripts to
    // the agent so it should already be in enabled state by then.
    m_enabled = true;
    m_state->setBoolean(DebuggerAgentState::debuggerEnabled, true);
    debugger().debuggerAgentEnabled();

    protocol::Vector<V8DebuggerParsedScript> compiledScripts;
    debugger().getCompiledScripts(m_session->contextGroupId(), compiledScripts);
    for (size_t i = 0; i < compiledScripts.size(); i++)
        didParseSource(compiledScripts[i]);

    // FIXME(WK44513): breakpoints activated flag should be synchronized between all front-ends
    debugger().setBreakpointsActivated(true);
    m_session->changeInstrumentationCounter(+1);
}

bool V8DebuggerAgentImpl::enabled()
{
    return m_enabled;
}

void V8DebuggerAgentImpl::enable(ErrorString* errorString)
{
    if (enabled())
        return;

    if (!m_session->client()->canExecuteScripts()) {
        *errorString = "Script execution is prohibited";
        return;
    }

    enable();
    ASSERT(m_frontend);
}

void V8DebuggerAgentImpl::disable(ErrorString*)
{
    if (!enabled())
        return;
    m_session->changeInstrumentationCounter(-1);

    m_state->setObject(DebuggerAgentState::javaScriptBreakpoints, protocol::DictionaryValue::create());
    m_state->setNumber(DebuggerAgentState::pauseOnExceptionsState, V8DebuggerImpl::DontPauseOnExceptions);
    m_state->setNumber(DebuggerAgentState::asyncCallStackDepth, 0);

    if (!m_pausedContext.IsEmpty())
        debugger().continueProgram();
    debugger().debuggerAgentDisabled();
    m_pausedContext.Reset();
    JavaScriptCallFrames emptyCallFrames;
    m_pausedCallFrames.swap(emptyCallFrames);
    m_scripts.clear();
    m_blackboxedPositions.clear();
    m_breakpointIdToDebuggerBreakpointIds.clear();
    internalSetAsyncCallStackDepth(0);
    m_continueToLocationBreakpointId = String16();
    clearBreakDetails();
    m_scheduledDebuggerStep = NoStep;
    m_skipNextDebuggerStepOut = false;
    m_javaScriptPauseScheduled = false;
    m_steppingFromFramework = false;
    m_pausingOnNativeEvent = false;
    m_skippedStepFrameCount = 0;
    m_recursionLevelForStepFrame = 0;
    m_skipAllPauses = false;
    m_blackboxPattern = nullptr;
    m_state->remove(DebuggerAgentState::blackboxPattern);
    m_enabled = false;
    m_state->setBoolean(DebuggerAgentState::debuggerEnabled, false);
}

void V8DebuggerAgentImpl::internalSetAsyncCallStackDepth(int depth)
{
    if (depth <= 0) {
        m_maxAsyncCallStackDepth = 0;
        allAsyncTasksCanceled();
    } else {
        m_maxAsyncCallStackDepth = depth;
    }
}

void V8DebuggerAgentImpl::setInspectorState(protocol::DictionaryValue* state)
{
    m_state = state;
}

void V8DebuggerAgentImpl::clearFrontend()
{
    ErrorString error;
    disable(&error);
    ASSERT(m_frontend);
    m_frontend = nullptr;
}

void V8DebuggerAgentImpl::restore()
{
    ASSERT(!m_enabled);
    if (!m_state->booleanProperty(DebuggerAgentState::debuggerEnabled, false))
        return;
    if (!m_session->client()->canExecuteScripts())
        return;

    enable();
    ErrorString error;

    int pauseState = V8DebuggerImpl::DontPauseOnExceptions;
    m_state->getNumber(DebuggerAgentState::pauseOnExceptionsState, &pauseState);
    setPauseOnExceptionsImpl(&error, pauseState);
    ASSERT(error.isEmpty());

    m_skipAllPauses = m_state->booleanProperty(DebuggerAgentState::skipAllPauses, false);

    int asyncCallStackDepth = 0;
    m_state->getNumber(DebuggerAgentState::asyncCallStackDepth, &asyncCallStackDepth);
    internalSetAsyncCallStackDepth(asyncCallStackDepth);

    String16 blackboxPattern;
    if (m_state->getString(DebuggerAgentState::blackboxPattern, &blackboxPattern)) {
        if (!setBlackboxPattern(&error, blackboxPattern))
            ASSERT_NOT_REACHED();
    }
}

void V8DebuggerAgentImpl::setBreakpointsActive(ErrorString* errorString, bool active)
{
    if (!checkEnabled(errorString))
        return;
    debugger().setBreakpointsActivated(active);
}

void V8DebuggerAgentImpl::setSkipAllPauses(ErrorString*, bool skipped)
{
    m_skipAllPauses = skipped;
    m_state->setBoolean(DebuggerAgentState::skipAllPauses, m_skipAllPauses);
}

static PassOwnPtr<protocol::DictionaryValue> buildObjectForBreakpointCookie(const String16& url, int lineNumber, int columnNumber, const String16& condition, bool isRegex)
{
    OwnPtr<protocol::DictionaryValue> breakpointObject = protocol::DictionaryValue::create();
    breakpointObject->setString(DebuggerAgentState::url, url);
    breakpointObject->setNumber(DebuggerAgentState::lineNumber, lineNumber);
    breakpointObject->setNumber(DebuggerAgentState::columnNumber, columnNumber);
    breakpointObject->setString(DebuggerAgentState::condition, condition);
    breakpointObject->setBoolean(DebuggerAgentState::isRegex, isRegex);
    return breakpointObject.release();
}

static bool matches(V8DebuggerImpl* debugger, const String16& url, const String16& pattern, bool isRegex)
{
    if (isRegex) {
        V8Regex regex(debugger, pattern, true);
        return regex.match(url) != -1;
    }
    return url == pattern;
}

void V8DebuggerAgentImpl::setBreakpointByUrl(ErrorString* errorString,
    int lineNumber,
    const Maybe<String16>& optionalURL,
    const Maybe<String16>& optionalURLRegex,
    const Maybe<int>& optionalColumnNumber,
    const Maybe<String16>& optionalCondition,
    String16* outBreakpointId,
    OwnPtr<protocol::Array<protocol::Debugger::Location>>* locations)
{
    *locations = Array<protocol::Debugger::Location>::create();
    if (optionalURL.isJust() == optionalURLRegex.isJust()) {
        *errorString = "Either url or urlRegex must be specified.";
        return;
    }

    String16 url = optionalURL.isJust() ? optionalURL.fromJust() : optionalURLRegex.fromJust();
    int columnNumber = 0;
    if (optionalColumnNumber.isJust()) {
        columnNumber = optionalColumnNumber.fromJust();
        if (columnNumber < 0) {
            *errorString = "Incorrect column number";
            return;
        }
    }
    String16 condition = optionalCondition.fromMaybe("");
    bool isRegex = optionalURLRegex.isJust();

    String16 breakpointId = (isRegex ? "/" + url + "/" : url) + ":" + String16::number(lineNumber) + ":" + String16::number(columnNumber);
    protocol::DictionaryValue* breakpointsCookie = m_state->getObject(DebuggerAgentState::javaScriptBreakpoints);
    if (!breakpointsCookie) {
        OwnPtr<protocol::DictionaryValue> newValue = protocol::DictionaryValue::create();
        breakpointsCookie = newValue.get();
        m_state->setObject(DebuggerAgentState::javaScriptBreakpoints, newValue.release());
    }
    if (breakpointsCookie->get(breakpointId)) {
        *errorString = "Breakpoint at specified location already exists.";
        return;
    }

    breakpointsCookie->setObject(breakpointId, buildObjectForBreakpointCookie(url, lineNumber, columnNumber, condition, isRegex));

    ScriptBreakpoint breakpoint(lineNumber, columnNumber, condition);
    for (auto& script : m_scripts) {
        if (!matches(m_debugger, script.second->sourceURL(), url, isRegex))
            continue;
        OwnPtr<protocol::Debugger::Location> location = resolveBreakpoint(breakpointId, script.first, breakpoint, UserBreakpointSource);
        if (location)
            (*locations)->addItem(location.release());
    }

    *outBreakpointId = breakpointId;
}

static bool parseLocation(ErrorString* errorString, PassOwnPtr<protocol::Debugger::Location> location, String16* scriptId, int* lineNumber, int* columnNumber)
{
    *scriptId = location->getScriptId();
    *lineNumber = location->getLineNumber();
    *columnNumber = location->getColumnNumber(0);
    return true;
}

void V8DebuggerAgentImpl::setBreakpoint(ErrorString* errorString,
    PassOwnPtr<protocol::Debugger::Location> location,
    const Maybe<String16>& optionalCondition,
    String16* outBreakpointId,
    OwnPtr<protocol::Debugger::Location>* actualLocation)
{
    String16 scriptId;
    int lineNumber;
    int columnNumber;

    if (!parseLocation(errorString, std::move(location), &scriptId, &lineNumber, &columnNumber))
        return;

    String16 condition = optionalCondition.fromMaybe("");

    String16 breakpointId = generateBreakpointId(scriptId, lineNumber, columnNumber, UserBreakpointSource);
    if (m_breakpointIdToDebuggerBreakpointIds.contains(breakpointId)) {
        *errorString = "Breakpoint at specified location already exists.";
        return;
    }
    ScriptBreakpoint breakpoint(lineNumber, columnNumber, condition);
    *actualLocation = resolveBreakpoint(breakpointId, scriptId, breakpoint, UserBreakpointSource);
    if (*actualLocation)
        *outBreakpointId = breakpointId;
    else
        *errorString = "Could not resolve breakpoint";
}

void V8DebuggerAgentImpl::removeBreakpoint(ErrorString* errorString, const String16& breakpointId)
{
    if (!checkEnabled(errorString))
        return;
    protocol::DictionaryValue* breakpointsCookie = m_state->getObject(DebuggerAgentState::javaScriptBreakpoints);
    if (breakpointsCookie)
        breakpointsCookie->remove(breakpointId);
    removeBreakpoint(breakpointId);
}

void V8DebuggerAgentImpl::removeBreakpoint(const String16& breakpointId)
{
    ASSERT(enabled());
    BreakpointIdToDebuggerBreakpointIdsMap::iterator debuggerBreakpointIdsIterator = m_breakpointIdToDebuggerBreakpointIds.find(breakpointId);
    if (debuggerBreakpointIdsIterator == m_breakpointIdToDebuggerBreakpointIds.end())
        return;
    protocol::Vector<String16>* ids = debuggerBreakpointIdsIterator->second;
    for (size_t i = 0; i < ids->size(); ++i) {
        const String16& debuggerBreakpointId = ids->at(i);

        debugger().removeBreakpoint(debuggerBreakpointId);
        m_serverBreakpoints.remove(debuggerBreakpointId);
    }
    m_breakpointIdToDebuggerBreakpointIds.remove(breakpointId);
}

void V8DebuggerAgentImpl::continueToLocation(ErrorString* errorString,
    PassOwnPtr<protocol::Debugger::Location> location,
    const protocol::Maybe<bool>& interstateLocationOpt)
{
    if (!checkEnabled(errorString))
        return;
    if (!m_continueToLocationBreakpointId.isEmpty()) {
        debugger().removeBreakpoint(m_continueToLocationBreakpointId);
        m_continueToLocationBreakpointId = "";
    }

    String16 scriptId;
    int lineNumber;
    int columnNumber;

    if (!parseLocation(errorString, std::move(location), &scriptId, &lineNumber, &columnNumber))
        return;

    ScriptBreakpoint breakpoint(lineNumber, columnNumber, "");
    m_continueToLocationBreakpointId = debugger().setBreakpoint(scriptId, breakpoint, &lineNumber, &columnNumber, interstateLocationOpt.fromMaybe(false));
    resume(errorString);
}

void V8DebuggerAgentImpl::getBacktrace(ErrorString* errorString, OwnPtr<Array<CallFrame>>* callFrames, Maybe<StackTrace>* asyncStackTrace)
{
    if (!assertPaused(errorString))
        return;
    m_pausedCallFrames.swap(debugger().currentCallFrames());
    *callFrames = currentCallFrames(errorString);
    if (!*callFrames)
        return;
    *asyncStackTrace = currentAsyncStackTrace();
}

bool V8DebuggerAgentImpl::isCurrentCallStackEmptyOrBlackboxed()
{
    ASSERT(enabled());
    JavaScriptCallFrames callFrames = debugger().currentCallFrames();
    for (size_t index = 0; index < callFrames.size(); ++index) {
        if (!isCallFrameWithUnknownScriptOrBlackboxed(callFrames[index].get()))
            return false;
    }
    return true;
}

bool V8DebuggerAgentImpl::isTopPausedCallFrameBlackboxed()
{
    ASSERT(enabled());
    return isCallFrameWithUnknownScriptOrBlackboxed(m_pausedCallFrames.size() ? m_pausedCallFrames[0].get() : nullptr);
}

bool V8DebuggerAgentImpl::isCallFrameWithUnknownScriptOrBlackboxed(JavaScriptCallFrame* frame)
{
    if (!frame)
        return true;
    ScriptsMap::iterator it = m_scripts.find(String16::number(frame->sourceID()));
    if (it == m_scripts.end()) {
        // Unknown scripts are blackboxed.
        return true;
    }
    if (m_blackboxPattern) {
        String16 scriptSourceURL = it->second->sourceURL();
        if (!scriptSourceURL.isEmpty() && m_blackboxPattern->match(scriptSourceURL) != -1)
            return true;
    }
    auto itBlackboxedPositions = m_blackboxedPositions.find(String16::number(frame->sourceID()));
    if (itBlackboxedPositions == m_blackboxedPositions.end())
        return false;

    protocol::Vector<std::pair<int, int>>* ranges = itBlackboxedPositions->second;
    auto itRange = std::lower_bound(ranges->begin(), ranges->end(), std::make_pair(frame->line(), frame->column()), positionComparator);
    // Ranges array contains positions in script where blackbox state is changed.
    // [(0,0) ... ranges[0]) isn't blackboxed, [ranges[0] ... ranges[1]) is blackboxed...
    return std::distance(ranges->begin(), itRange) % 2;
}

V8DebuggerAgentImpl::SkipPauseRequest V8DebuggerAgentImpl::shouldSkipExceptionPause(JavaScriptCallFrame* topCallFrame)
{
    if (m_steppingFromFramework)
        return RequestNoSkip;
    if (isCallFrameWithUnknownScriptOrBlackboxed(topCallFrame))
        return RequestContinue;
    return RequestNoSkip;
}

V8DebuggerAgentImpl::SkipPauseRequest V8DebuggerAgentImpl::shouldSkipStepPause(JavaScriptCallFrame* topCallFrame)
{
    if (m_steppingFromFramework)
        return RequestNoSkip;

    if (m_skipNextDebuggerStepOut) {
        m_skipNextDebuggerStepOut = false;
        if (m_scheduledDebuggerStep == StepOut)
            return RequestStepOut;
    }

    if (!isCallFrameWithUnknownScriptOrBlackboxed(topCallFrame))
        return RequestNoSkip;

    if (m_skippedStepFrameCount >= maxSkipStepFrameCount)
        return RequestStepOut;

    if (!m_skippedStepFrameCount)
        m_recursionLevelForStepFrame = 1;

    ++m_skippedStepFrameCount;
    return RequestStepFrame;
}

PassOwnPtr<protocol::Debugger::Location> V8DebuggerAgentImpl::resolveBreakpoint(const String16& breakpointId, const String16& scriptId, const ScriptBreakpoint& breakpoint, BreakpointSource source)
{
    ASSERT(enabled());
    // FIXME: remove these checks once crbug.com/520702 is resolved.
    RELEASE_ASSERT(!breakpointId.isEmpty());
    RELEASE_ASSERT(!scriptId.isEmpty());
    ScriptsMap::iterator scriptIterator = m_scripts.find(scriptId);
    if (scriptIterator == m_scripts.end())
        return nullptr;
    V8DebuggerScript* script = scriptIterator->second;
    if (breakpoint.lineNumber < script->startLine() || script->endLine() < breakpoint.lineNumber)
        return nullptr;

    int actualLineNumber;
    int actualColumnNumber;
    String16 debuggerBreakpointId = debugger().setBreakpoint(scriptId, breakpoint, &actualLineNumber, &actualColumnNumber, false);
    if (debuggerBreakpointId.isEmpty())
        return nullptr;

    m_serverBreakpoints.set(debuggerBreakpointId, std::make_pair(breakpointId, source));
    RELEASE_ASSERT(!breakpointId.isEmpty());
    if (!m_breakpointIdToDebuggerBreakpointIds.contains(breakpointId))
        m_breakpointIdToDebuggerBreakpointIds.set(breakpointId, protocol::Vector<String16>());

    BreakpointIdToDebuggerBreakpointIdsMap::iterator debuggerBreakpointIdsIterator = m_breakpointIdToDebuggerBreakpointIds.find(breakpointId);
    debuggerBreakpointIdsIterator->second->append(debuggerBreakpointId);

    return buildProtocolLocation(scriptId, actualLineNumber, actualColumnNumber);
}

void V8DebuggerAgentImpl::searchInContent(ErrorString* error, const String16& scriptId, const String16& query,
    const Maybe<bool>& optionalCaseSensitive,
    const Maybe<bool>& optionalIsRegex,
    OwnPtr<Array<protocol::Debugger::SearchMatch>>* results)
{
    ScriptsMap::iterator it = m_scripts.find(scriptId);
    if (it != m_scripts.end())
        *results = V8ContentSearchUtil::searchInTextByLines(m_session, it->second->source(), query, optionalCaseSensitive.fromMaybe(false), optionalIsRegex.fromMaybe(false));
    else
        *error = String16("No script for id: " + scriptId);
}

void V8DebuggerAgentImpl::setScriptSource(ErrorString* errorString,
    const String16& scriptId,
    const String16& newContent,
    const Maybe<bool>& preview,
    Maybe<protocol::Array<protocol::Debugger::CallFrame>>* newCallFrames,
    Maybe<bool>* stackChanged,
    Maybe<StackTrace>* asyncStackTrace,
    Maybe<protocol::Debugger::SetScriptSourceError>* optOutCompileError)
{
    if (!checkEnabled(errorString))
        return;
    if (!debugger().setScriptSource(scriptId, newContent, preview.fromMaybe(false), errorString, optOutCompileError, &m_pausedCallFrames, stackChanged))
        return;

    OwnPtr<Array<CallFrame>> callFrames = currentCallFrames(errorString);
    if (!callFrames)
        return;
    *newCallFrames = callFrames.release();
    *asyncStackTrace = currentAsyncStackTrace();

    ScriptsMap::iterator it = m_scripts.find(scriptId);
    if (it == m_scripts.end())
        return;
    it->second->setSource(newContent);
}

void V8DebuggerAgentImpl::restartFrame(ErrorString* errorString,
    const String16& callFrameId,
    OwnPtr<Array<CallFrame>>* newCallFrames,
    Maybe<StackTrace>* asyncStackTrace)
{
    if (!assertPaused(errorString))
        return;
    InjectedScript::CallFrameScope scope(errorString, m_debugger, m_session->contextGroupId(), callFrameId);
    if (!scope.initialize())
        return;
    if (scope.frameOrdinal() >= m_pausedCallFrames.size()) {
        *errorString = "Could not find call frame with given id";
        return;
    }

    v8::Local<v8::Value> resultValue;
    v8::Local<v8::Boolean> result;
    if (!m_pausedCallFrames[scope.frameOrdinal()].get()->restart().ToLocal(&resultValue) || scope.tryCatch().HasCaught() || !resultValue->ToBoolean(scope.context()).ToLocal(&result) || !result->Value()) {
        *errorString = "Internal error";
        return;
    }
    m_pausedCallFrames.swap(debugger().currentCallFrames());

    *newCallFrames = currentCallFrames(errorString);
    if (!*newCallFrames)
        return;
    *asyncStackTrace = currentAsyncStackTrace();
}

void V8DebuggerAgentImpl::getScriptSource(ErrorString* error, const String16& scriptId, String16* scriptSource)
{
    if (!checkEnabled(error))
        return;
    ScriptsMap::iterator it = m_scripts.find(scriptId);
    if (it == m_scripts.end()) {
        *error = "No script for id: " + scriptId;
        return;
    }
    *scriptSource = it->second->source();
}

void V8DebuggerAgentImpl::getFunctionDetails(ErrorString* errorString, const String16& functionId, OwnPtr<FunctionDetails>* details)
{
    if (!checkEnabled(errorString))
        return;
    InjectedScript::ObjectScope scope(errorString, m_debugger, m_session->contextGroupId(), functionId);
    if (!scope.initialize())
        return;
    if (!scope.object()->IsFunction()) {
        *errorString = "Value with given id is not a function";
        return;
    }
    v8::Local<v8::Function> function = scope.object().As<v8::Function>();

    v8::Local<v8::Value> scopesValue;
    v8::Local<v8::Array> scopes;
    if (m_debugger->functionScopes(function).ToLocal(&scopesValue) && scopesValue->IsArray()) {
        scopes = scopesValue.As<v8::Array>();
        if (!scope.injectedScript()->wrapPropertyInArray(errorString, scopes, toV8StringInternalized(m_isolate, "object"), scope.objectGroupName()))
            return;
    }

    OwnPtr<FunctionDetails> functionDetails = FunctionDetails::create()
        .setLocation(buildProtocolLocation(String16::number(function->ScriptId()), function->GetScriptLineNumber(), function->GetScriptColumnNumber()))
        .setFunctionName(toProtocolStringWithTypeCheck(function->GetDebugName()))
        .setIsGenerator(function->IsGeneratorFunction()).build();

    if (!scopes.IsEmpty()) {
        protocol::ErrorSupport errorSupport;
        OwnPtr<protocol::Array<protocol::Debugger::Scope>> scopeChain = protocol::Array<protocol::Debugger::Scope>::parse(toProtocolValue(scope.context(), scopes).get(), &errorSupport);
        if (hasInternalError(errorString, errorSupport.hasErrors()))
            return;
        functionDetails->setScopeChain(scopeChain.release());
    }

    *details = functionDetails.release();
}

void V8DebuggerAgentImpl::getGeneratorObjectDetails(ErrorString* errorString, const String16& objectId, OwnPtr<GeneratorObjectDetails>* outDetails)
{
    if (!checkEnabled(errorString))
        return;
    InjectedScript::ObjectScope scope(errorString, m_debugger, m_session->contextGroupId(), objectId);
    if (!scope.initialize())
        return;
    if (!scope.object()->IsObject()) {
        *errorString = "Value with given id is not an Object";
        return;
    }
    v8::Local<v8::Object> object = scope.object().As<v8::Object>();

    v8::Local<v8::Object> detailsObject;
    v8::Local<v8::Value> detailsValue = debugger().generatorObjectDetails(object);
    if (hasInternalError(errorString, !detailsValue->IsObject() || !detailsValue->ToObject(scope.context()).ToLocal(&detailsObject)))
        return;

    if (!scope.injectedScript()->wrapObjectProperty(errorString, detailsObject, toV8StringInternalized(m_isolate, "function"), scope.objectGroupName()))
        return;

    protocol::ErrorSupport errors;
    OwnPtr<GeneratorObjectDetails> protocolDetails = GeneratorObjectDetails::parse(toProtocolValue(scope.context(), detailsObject).get(), &errors);
    if (hasInternalError(errorString, !protocolDetails))
        return;
    *outDetails = protocolDetails.release();
}

void V8DebuggerAgentImpl::getCollectionEntries(ErrorString* errorString, const String16& objectId, OwnPtr<protocol::Array<CollectionEntry>>* outEntries)
{
    if (!checkEnabled(errorString))
        return;
    InjectedScript::ObjectScope scope(errorString, m_debugger, m_session->contextGroupId(), objectId);
    if (!scope.initialize())
        return;
    if (!scope.object()->IsObject()) {
        *errorString = "Object with given id is not a collection";
        return;
    }
    v8::Local<v8::Object> object = scope.object().As<v8::Object>();

    v8::Local<v8::Value> entriesValue = m_debugger->collectionEntries(object);
    if (hasInternalError(errorString, entriesValue.IsEmpty()))
        return;
    if (entriesValue->IsUndefined()) {
        *errorString = "Object with given id is not a collection";
        return;
    }
    if (hasInternalError(errorString, !entriesValue->IsArray()))
        return;
    v8::Local<v8::Array> entriesArray = entriesValue.As<v8::Array>();
    if (!scope.injectedScript()->wrapPropertyInArray(errorString, entriesArray, toV8StringInternalized(m_isolate, "key"), scope.objectGroupName()))
        return;
    if (!scope.injectedScript()->wrapPropertyInArray(errorString, entriesArray, toV8StringInternalized(m_isolate, "value"), scope.objectGroupName()))
        return;
    protocol::ErrorSupport errors;
    OwnPtr<protocol::Array<CollectionEntry>> entries = protocol::Array<CollectionEntry>::parse(toProtocolValue(scope.context(), entriesArray).get(), &errors);
    if (hasInternalError(errorString, !entries))
        return;
    *outEntries = entries.release();
}

void V8DebuggerAgentImpl::schedulePauseOnNextStatement(const String16& breakReason, PassOwnPtr<protocol::DictionaryValue> data)
{
    if (!enabled() || m_scheduledDebuggerStep == StepInto || m_javaScriptPauseScheduled || debugger().isPaused() || !debugger().breakpointsActivated())
        return;
    m_breakReason = breakReason;
    m_breakAuxData = std::move(data);
    m_pausingOnNativeEvent = true;
    m_skipNextDebuggerStepOut = false;
    debugger().setPauseOnNextStatement(true);
}

void V8DebuggerAgentImpl::schedulePauseOnNextStatementIfSteppingInto()
{
    ASSERT(enabled());
    if (m_scheduledDebuggerStep != StepInto || m_javaScriptPauseScheduled || debugger().isPaused())
        return;
    clearBreakDetails();
    m_pausingOnNativeEvent = false;
    m_skippedStepFrameCount = 0;
    m_recursionLevelForStepFrame = 0;
    debugger().setPauseOnNextStatement(true);
}

void V8DebuggerAgentImpl::cancelPauseOnNextStatement()
{
    if (m_javaScriptPauseScheduled || debugger().isPaused())
        return;
    clearBreakDetails();
    m_pausingOnNativeEvent = false;
    debugger().setPauseOnNextStatement(false);
}

bool V8DebuggerAgentImpl::v8AsyncTaskEventsEnabled() const
{
    return m_maxAsyncCallStackDepth;
}

void V8DebuggerAgentImpl::didReceiveV8AsyncTaskEvent(v8::Local<v8::Context> context, const String16& eventType, const String16& eventName, int id)
{
    ASSERT(m_maxAsyncCallStackDepth);
    // The scopes for the ids are defined by the eventName namespaces. There are currently two namespaces: "Object." and "Promise.".
    void* ptr = reinterpret_cast<void*>(id * 4 + (eventName[0] == 'P' ? 2 : 0) + 1);
    if (eventType == v8AsyncTaskEventEnqueue)
        asyncTaskScheduled(eventName, ptr, false);
    else if (eventType == v8AsyncTaskEventWillHandle)
        asyncTaskStarted(ptr);
    else if (eventType == v8AsyncTaskEventDidHandle)
        asyncTaskFinished(ptr);
    else
        ASSERT_NOT_REACHED();
}

void V8DebuggerAgentImpl::pause(ErrorString* errorString)
{
    if (!checkEnabled(errorString))
        return;
    if (m_javaScriptPauseScheduled || debugger().isPaused())
        return;
    clearBreakDetails();
    m_javaScriptPauseScheduled = true;
    m_scheduledDebuggerStep = NoStep;
    m_skippedStepFrameCount = 0;
    m_steppingFromFramework = false;
    debugger().setPauseOnNextStatement(true);
}

void V8DebuggerAgentImpl::resume(ErrorString* errorString)
{
    if (!assertPaused(errorString))
        return;
    m_scheduledDebuggerStep = NoStep;
    m_steppingFromFramework = false;
    m_session->releaseObjectGroup(V8InspectorSession::backtraceObjectGroup);
    debugger().continueProgram();
}

void V8DebuggerAgentImpl::stepOver(ErrorString* errorString)
{
    if (!assertPaused(errorString))
        return;
    // StepOver at function return point should fallback to StepInto.
    JavaScriptCallFrame* frame = m_pausedCallFrames.size() ? m_pausedCallFrames[0].get() : nullptr;
    if (frame && frame->isAtReturn()) {
        stepInto(errorString);
        return;
    }
    m_scheduledDebuggerStep = StepOver;
    m_steppingFromFramework = isTopPausedCallFrameBlackboxed();
    m_session->releaseObjectGroup(V8InspectorSession::backtraceObjectGroup);
    debugger().stepOverStatement();
}

void V8DebuggerAgentImpl::stepInto(ErrorString* errorString)
{
    if (!assertPaused(errorString))
        return;
    m_scheduledDebuggerStep = StepInto;
    m_steppingFromFramework = isTopPausedCallFrameBlackboxed();
    m_session->releaseObjectGroup(V8InspectorSession::backtraceObjectGroup);
    debugger().stepIntoStatement();
}

void V8DebuggerAgentImpl::stepOut(ErrorString* errorString)
{
    if (!assertPaused(errorString))
        return;
    m_scheduledDebuggerStep = StepOut;
    m_skipNextDebuggerStepOut = false;
    m_recursionLevelForStepOut = 1;
    m_steppingFromFramework = isTopPausedCallFrameBlackboxed();
    m_session->releaseObjectGroup(V8InspectorSession::backtraceObjectGroup);
    debugger().stepOutOfFunction();
}

void V8DebuggerAgentImpl::setPauseOnExceptions(ErrorString* errorString, const String16& stringPauseState)
{
    if (!checkEnabled(errorString))
        return;
    V8DebuggerImpl::PauseOnExceptionsState pauseState;
    if (stringPauseState == "none") {
        pauseState = V8DebuggerImpl::DontPauseOnExceptions;
    } else if (stringPauseState == "all") {
        pauseState = V8DebuggerImpl::PauseOnAllExceptions;
    } else if (stringPauseState == "uncaught") {
        pauseState = V8DebuggerImpl::PauseOnUncaughtExceptions;
    } else {
        *errorString = "Unknown pause on exceptions mode: " + stringPauseState;
        return;
    }
    setPauseOnExceptionsImpl(errorString, pauseState);
}

void V8DebuggerAgentImpl::setPauseOnExceptionsImpl(ErrorString* errorString, int pauseState)
{
    debugger().setPauseOnExceptionsState(static_cast<V8DebuggerImpl::PauseOnExceptionsState>(pauseState));
    if (debugger().getPauseOnExceptionsState() != pauseState)
        *errorString = "Internal error. Could not change pause on exceptions state";
    else
        m_state->setNumber(DebuggerAgentState::pauseOnExceptionsState, pauseState);
}

void V8DebuggerAgentImpl::evaluateOnCallFrame(ErrorString* errorString,
    const String16& callFrameId,
    const String16& expression,
    const Maybe<String16>& objectGroup,
    const Maybe<bool>& includeCommandLineAPI,
    const Maybe<bool>& doNotPauseOnExceptionsAndMuteConsole,
    const Maybe<bool>& returnByValue,
    const Maybe<bool>& generatePreview,
    OwnPtr<RemoteObject>* result,
    Maybe<bool>* wasThrown,
    Maybe<protocol::Runtime::ExceptionDetails>* exceptionDetails)
{
    if (!assertPaused(errorString))
        return;
    InjectedScript::CallFrameScope scope(errorString, m_debugger, m_session->contextGroupId(), callFrameId);
    if (!scope.initialize())
        return;
    if (scope.frameOrdinal() >= m_pausedCallFrames.size()) {
        *errorString = "Could not find call frame with given id";
        return;
    }

    if (includeCommandLineAPI.fromMaybe(false) && !scope.installCommandLineAPI())
        return;
    if (doNotPauseOnExceptionsAndMuteConsole.fromMaybe(false))
        scope.ignoreExceptionsAndMuteConsole();

    v8::MaybeLocal<v8::Value> maybeResultValue = m_pausedCallFrames[scope.frameOrdinal()].get()->evaluate(toV8String(m_isolate, expression));

    // Re-initialize after running client's code, as it could have destroyed context or session.
    if (!scope.initialize())
        return;
    scope.injectedScript()->wrapEvaluateResult(errorString,
        maybeResultValue,
        scope.tryCatch(),
        objectGroup.fromMaybe(""),
        returnByValue.fromMaybe(false),
        generatePreview.fromMaybe(false),
        result,
        wasThrown,
        exceptionDetails);
}

void V8DebuggerAgentImpl::setVariableValue(ErrorString* errorString,
    int scopeNumber,
    const String16& variableName,
    PassOwnPtr<protocol::Runtime::CallArgument> newValueArgument,
    const String16& callFrameId)
{
    if (!checkEnabled(errorString))
        return;
    if (!assertPaused(errorString))
        return;
    InjectedScript::CallFrameScope scope(errorString, m_debugger, m_session->contextGroupId(), callFrameId);
    if (!scope.initialize())
        return;

    v8::Local<v8::Value> newValue;
    if (!scope.injectedScript()->resolveCallArgument(errorString, newValueArgument.get()).ToLocal(&newValue))
        return;

    if (scope.frameOrdinal() >= m_pausedCallFrames.size()) {
        *errorString = "Could not find call frame with given id";
        return;
    }
    v8::MaybeLocal<v8::Value> result = m_pausedCallFrames[scope.frameOrdinal()].get()->setVariableValue(scopeNumber, toV8String(m_isolate, variableName), newValue);
    if (scope.tryCatch().HasCaught() || result.IsEmpty()) {
        *errorString = "Internal error";
        return;
    }
}

void V8DebuggerAgentImpl::setAsyncCallStackDepth(ErrorString* errorString, int depth)
{
    if (!checkEnabled(errorString))
        return;
    m_state->setNumber(DebuggerAgentState::asyncCallStackDepth, depth);
    internalSetAsyncCallStackDepth(depth);
}

void V8DebuggerAgentImpl::asyncTaskScheduled(const String16& taskName, void* task, bool recurring)
{
    if (!m_maxAsyncCallStackDepth)
        return;
    v8::HandleScope scope(m_isolate);
    OwnPtr<V8StackTraceImpl> chain = V8StackTraceImpl::capture(this, V8StackTrace::maxCallStackSizeToCapture, taskName);
    if (chain) {
        m_asyncTaskStacks.set(task, chain.release());
        if (recurring)
            m_recurringTasks.add(task);
    }
}

void V8DebuggerAgentImpl::asyncTaskCanceled(void* task)
{
    if (!m_maxAsyncCallStackDepth)
        return;
    m_asyncTaskStacks.remove(task);
    m_recurringTasks.remove(task);
}

void V8DebuggerAgentImpl::asyncTaskStarted(void* task)
{
    // Not enabled, return.
    if (!m_maxAsyncCallStackDepth)
        return;

#if ENABLE(ASSERT)
    m_currentTasks.append(task);
#endif

    V8StackTraceImpl* stack = m_asyncTaskStacks.get(task);
    // Needs to support following order of events:
    // - asyncTaskScheduled
    //   <-- attached here -->
    // - asyncTaskStarted
    // - asyncTaskCanceled <-- canceled before finished
    //   <-- async stack requested here -->
    // - asyncTaskFinished
    m_currentStacks.append(stack ? stack->clone() : nullptr);
}

void V8DebuggerAgentImpl::asyncTaskFinished(void* task)
{
    if (!m_maxAsyncCallStackDepth)
        return;
    // We could start instrumenting half way and the stack is empty.
    if (!m_currentStacks.size())
        return;

#if ENABLE(ASSERT)
    ASSERT(m_currentTasks.last() == task);
    m_currentTasks.removeLast();
#endif

    m_currentStacks.removeLast();
    if (!m_recurringTasks.contains(task))
        m_asyncTaskStacks.remove(task);
}

void V8DebuggerAgentImpl::allAsyncTasksCanceled()
{
    m_asyncTaskStacks.clear();
    m_recurringTasks.clear();
    m_currentStacks.clear();

#if ENABLE(ASSERT)
    m_currentTasks.clear();
#endif
}

void V8DebuggerAgentImpl::setBlackboxPatterns(ErrorString* errorString, PassOwnPtr<protocol::Array<String16>> patterns)
{
    if (!patterns->length()) {
        m_blackboxPattern = nullptr;
        m_state->remove(DebuggerAgentState::blackboxPattern);
        return;
    }

    String16Builder patternBuilder;
    patternBuilder.append("(");
    for (size_t i = 0; i < patterns->length() - 1; ++i)
        patternBuilder.append(patterns->get(i) + "|");
    patternBuilder.append(patterns->get(patterns->length() - 1) + ")");
    String16 pattern = patternBuilder.toString();
    if (!setBlackboxPattern(errorString, pattern))
        return;
    m_state->setString(DebuggerAgentState::blackboxPattern, pattern);
}

bool V8DebuggerAgentImpl::setBlackboxPattern(ErrorString* errorString, const String16& pattern)
{
    OwnPtr<V8Regex> regex = adoptPtr(new V8Regex(m_debugger, pattern, true /** caseSensitive */, false /** multiline */));
    if (!regex->isValid()) {
        *errorString = "Pattern parser error: " + regex->errorMessage();
        return false;
    }
    m_blackboxPattern = regex.release();
    return true;
}

void V8DebuggerAgentImpl::setBlackboxedRanges(ErrorString* error, const String16& scriptId, PassOwnPtr<protocol::Array<protocol::Debugger::ScriptPosition>> inPositions)
{
    if (!m_scripts.contains(scriptId)) {
        *error = "No script with passed id.";
        return;
    }

    if (!inPositions->length()) {
        m_blackboxedPositions.remove(scriptId);
        return;
    }

    protocol::Vector<std::pair<int, int>> positions(inPositions->length());
    for (size_t i = 0; i < positions.size(); ++i) {
        protocol::Debugger::ScriptPosition* position = inPositions->get(i);
        if (position->getLine() < 0) {
            *error = "Position missing 'line' or 'line' < 0.";
            return;
        }
        if (position->getColumn() < 0) {
            *error = "Position missing 'column' or 'column' < 0.";
            return;
        }
        positions[i] = std::make_pair(position->getLine(), position->getColumn());
    }

    for (size_t i = 1; i < positions.size(); ++i) {
        if (positions[i - 1].first < positions[i].first)
            continue;
        if (positions[i - 1].first == positions[i].first && positions[i - 1].second < positions[i].second)
            continue;
        *error = "Input positions array is not sorted or contains duplicate values.";
        return;
    }

    m_blackboxedPositions.set(scriptId, positions);
}

void V8DebuggerAgentImpl::willExecuteScript(int scriptId)
{
    changeJavaScriptRecursionLevel(+1);
    // Fast return.
    if (m_scheduledDebuggerStep != StepInto)
        return;
    // Skip unknown scripts (e.g. InjectedScript).
    if (!m_scripts.contains(String16::number(scriptId)))
        return;
    schedulePauseOnNextStatementIfSteppingInto();
}

void V8DebuggerAgentImpl::didExecuteScript()
{
    changeJavaScriptRecursionLevel(-1);
}

void V8DebuggerAgentImpl::changeJavaScriptRecursionLevel(int step)
{
    if (m_javaScriptPauseScheduled && !m_skipAllPauses && !debugger().isPaused()) {
        // Do not ever loose user's pause request until we have actually paused.
        debugger().setPauseOnNextStatement(true);
    }
    if (m_scheduledDebuggerStep == StepOut) {
        m_recursionLevelForStepOut += step;
        if (!m_recursionLevelForStepOut) {
            // When StepOut crosses a task boundary (i.e. js -> blink_c++) from where it was requested,
            // switch stepping to step into a next JS task, as if we exited to a blackboxed framework.
            m_scheduledDebuggerStep = StepInto;
            m_skipNextDebuggerStepOut = false;
        }
    }
    if (m_recursionLevelForStepFrame) {
        m_recursionLevelForStepFrame += step;
        if (!m_recursionLevelForStepFrame) {
            // We have walked through a blackboxed framework and got back to where we started.
            // If there was no stepping scheduled, we should cancel the stepping explicitly,
            // since there may be a scheduled StepFrame left.
            // Otherwise, if we were stepping in/over, the StepFrame will stop at the right location,
            // whereas if we were stepping out, we should continue doing so after debugger pauses
            // from the old StepFrame.
            m_skippedStepFrameCount = 0;
            if (m_scheduledDebuggerStep == NoStep)
                debugger().clearStepping();
            else if (m_scheduledDebuggerStep == StepOut)
                m_skipNextDebuggerStepOut = true;
        }
    }
}

PassOwnPtr<Array<CallFrame>> V8DebuggerAgentImpl::currentCallFrames(ErrorString* errorString)
{
    if (m_pausedContext.IsEmpty() || !m_pausedCallFrames.size())
        return Array<CallFrame>::create();
    ErrorString ignored;
    InjectedScript* topFrameInjectedScript = m_session->findInjectedScript(&ignored, V8Debugger::contextId(m_pausedContext.Get(m_isolate)));
    if (!topFrameInjectedScript) {
        // Context has been reported as removed while on pause.
        return Array<CallFrame>::create();
    }

    v8::HandleScope handles(m_isolate);
    v8::Local<v8::Context> context = topFrameInjectedScript->context()->context();
    v8::Context::Scope contextScope(context);

    v8::Local<v8::Array> objects = v8::Array::New(m_isolate);
    for (size_t frameOrdinal = 0; frameOrdinal < m_pausedCallFrames.size(); ++frameOrdinal) {
        JavaScriptCallFrame* currentCallFrame = m_pausedCallFrames[frameOrdinal].get();

        v8::Local<v8::Object> details = currentCallFrame->details();
        if (hasInternalError(errorString, details.IsEmpty()))
            return Array<CallFrame>::create();

        int contextId = currentCallFrame->contextId();
        InjectedScript* injectedScript = contextId ? m_session->findInjectedScript(&ignored, contextId) : nullptr;
        if (!injectedScript)
            injectedScript = topFrameInjectedScript;

        String16 callFrameId = RemoteCallFrameId::serialize(injectedScript->context()->contextId(), frameOrdinal);
        if (hasInternalError(errorString, !details->Set(context, toV8StringInternalized(m_isolate, "callFrameId"), toV8String(m_isolate, callFrameId)).FromMaybe(false)))
            return Array<CallFrame>::create();

        v8::Local<v8::Value> scopeChain;
        if (hasInternalError(errorString, !details->Get(context, toV8StringInternalized(m_isolate, "scopeChain")).ToLocal(&scopeChain) || !scopeChain->IsArray()))
            return Array<CallFrame>::create();
        v8::Local<v8::Array> scopeChainArray = scopeChain.As<v8::Array>();
        if (!injectedScript->wrapPropertyInArray(errorString, scopeChainArray, toV8StringInternalized(m_isolate, "object"), V8InspectorSession::backtraceObjectGroup))
            return Array<CallFrame>::create();

        if (!injectedScript->wrapObjectProperty(errorString, details, toV8StringInternalized(m_isolate, "this"), V8InspectorSession::backtraceObjectGroup))
            return Array<CallFrame>::create();

        if (details->Has(context, toV8StringInternalized(m_isolate, "returnValue")).FromMaybe(false)) {
            if (!injectedScript->wrapObjectProperty(errorString, details, toV8StringInternalized(m_isolate, "returnValue"), V8InspectorSession::backtraceObjectGroup))
                return Array<CallFrame>::create();
        }

        if (hasInternalError(errorString, !objects->Set(context, frameOrdinal, details).FromMaybe(false)))
            return Array<CallFrame>::create();
    }

    protocol::ErrorSupport errorSupport;
    OwnPtr<Array<CallFrame>> callFrames = Array<CallFrame>::parse(toProtocolValue(context, objects).get(), &errorSupport);
    if (hasInternalError(errorString, !callFrames))
        return Array<CallFrame>::create();
    return callFrames.release();
}

PassOwnPtr<StackTrace> V8DebuggerAgentImpl::currentAsyncStackTrace()
{
    if (m_pausedContext.IsEmpty() || !m_maxAsyncCallStackDepth || !m_currentStacks.size() || !m_currentStacks.last())
        return nullptr;

    return m_currentStacks.last()->buildInspectorObjectForTail(this);
}

V8StackTraceImpl* V8DebuggerAgentImpl::currentAsyncCallChain()
{
    if (!m_currentStacks.size())
        return nullptr;
    return m_currentStacks.last().get();
}

void V8DebuggerAgentImpl::didParseSource(const V8DebuggerParsedScript& parsedScript)
{
    V8DebuggerScript script = parsedScript.script;

    bool isDeprecatedSourceURL = false;
    if (!parsedScript.success)
        script.setSourceURL(V8ContentSearchUtil::findSourceURL(script.source(), false, &isDeprecatedSourceURL));
    else if (script.hasSourceURL())
        V8ContentSearchUtil::findSourceURL(script.source(), false, &isDeprecatedSourceURL);

    bool isDeprecatedSourceMappingURL = false;
    if (!parsedScript.success)
        script.setSourceMappingURL(V8ContentSearchUtil::findSourceMapURL(script.source(), false, &isDeprecatedSourceMappingURL));
    else if (!script.sourceMappingURL().isEmpty())
        V8ContentSearchUtil::findSourceMapURL(script.source(), false, &isDeprecatedSourceMappingURL);

    script.setHash(calculateHash(script.source()));

    int executionContextId = script.executionContextId();
    bool isContentScript = script.isContentScript();
    bool isInternalScript = script.isInternalScript();
    bool isLiveEdit = script.isLiveEdit();
    bool hasSourceURL = script.hasSourceURL();
    String16 scriptURL = script.sourceURL();
    String16 sourceMapURL = script.sourceMappingURL();
    bool deprecatedCommentWasUsed = isDeprecatedSourceURL || isDeprecatedSourceMappingURL;

    const Maybe<String16>& sourceMapURLParam = sourceMapURL;
    const bool* isContentScriptParam = isContentScript ? &isContentScript : nullptr;
    const bool* isInternalScriptParam = isInternalScript ? &isInternalScript : nullptr;
    const bool* isLiveEditParam = isLiveEdit ? &isLiveEdit : nullptr;
    const bool* hasSourceURLParam = hasSourceURL ? &hasSourceURL : nullptr;
    const bool* deprecatedCommentWasUsedParam = deprecatedCommentWasUsed ? &deprecatedCommentWasUsed : nullptr;
    if (parsedScript.success)
        m_frontend->scriptParsed(parsedScript.scriptId, scriptURL, script.startLine(), script.startColumn(), script.endLine(), script.endColumn(), executionContextId, script.hash(), isContentScriptParam, isInternalScriptParam, isLiveEditParam, sourceMapURLParam, hasSourceURLParam, deprecatedCommentWasUsedParam);
    else
        m_frontend->scriptFailedToParse(parsedScript.scriptId, scriptURL, script.startLine(), script.startColumn(), script.endLine(), script.endColumn(), executionContextId, script.hash(), isContentScriptParam, isInternalScriptParam, sourceMapURLParam, hasSourceURLParam, deprecatedCommentWasUsedParam);

    m_scripts.set(parsedScript.scriptId, script);

    if (scriptURL.isEmpty() || !parsedScript.success)
        return;

    protocol::DictionaryValue* breakpointsCookie = m_state->getObject(DebuggerAgentState::javaScriptBreakpoints);
    if (!breakpointsCookie)
        return;

    for (size_t i = 0; i < breakpointsCookie->size(); ++i) {
        auto cookie = breakpointsCookie->at(i);
        protocol::DictionaryValue* breakpointObject = protocol::DictionaryValue::cast(cookie.second);
        bool isRegex;
        breakpointObject->getBoolean(DebuggerAgentState::isRegex, &isRegex);
        String16 url;
        breakpointObject->getString(DebuggerAgentState::url, &url);
        if (!matches(m_debugger, scriptURL, url, isRegex))
            continue;
        ScriptBreakpoint breakpoint;
        breakpointObject->getNumber(DebuggerAgentState::lineNumber, &breakpoint.lineNumber);
        breakpointObject->getNumber(DebuggerAgentState::columnNumber, &breakpoint.columnNumber);
        breakpointObject->getString(DebuggerAgentState::condition, &breakpoint.condition);
        OwnPtr<protocol::Debugger::Location> location = resolveBreakpoint(cookie.first, parsedScript.scriptId, breakpoint, UserBreakpointSource);
        if (location)
            m_frontend->breakpointResolved(cookie.first, location.release());
    }
}

V8DebuggerAgentImpl::SkipPauseRequest V8DebuggerAgentImpl::didPause(v8::Local<v8::Context> context, v8::Local<v8::Value> exception, const protocol::Vector<String16>& hitBreakpoints, bool isPromiseRejection)
{
    JavaScriptCallFrames callFrames = debugger().currentCallFrames(1);
    JavaScriptCallFrame* topCallFrame = callFrames.size() > 0 ? callFrames[0].get() : nullptr;

    V8DebuggerAgentImpl::SkipPauseRequest result;
    if (m_skipAllPauses)
        result = RequestContinue;
    else if (!hitBreakpoints.isEmpty())
        result = RequestNoSkip; // Don't skip explicit breakpoints even if set in frameworks.
    else if (!exception.IsEmpty())
        result = shouldSkipExceptionPause(topCallFrame);
    else if (m_scheduledDebuggerStep != NoStep || m_javaScriptPauseScheduled || m_pausingOnNativeEvent)
        result = shouldSkipStepPause(topCallFrame);
    else
        result = RequestNoSkip;

    m_skipNextDebuggerStepOut = false;
    if (result != RequestNoSkip)
        return result;
    // Skip pauses inside V8 internal scripts and on syntax errors.
    if (!topCallFrame)
        return RequestContinue;

    ASSERT(m_pausedContext.IsEmpty());
    m_pausedCallFrames.swap(debugger().currentCallFrames());
    m_pausedContext.Reset(m_isolate, context);
    v8::HandleScope handles(m_isolate);

    if (!exception.IsEmpty()) {
        ErrorString ignored;
        InjectedScript* injectedScript = m_session->findInjectedScript(&ignored, V8Debugger::contextId(context));
        if (injectedScript) {
            m_breakReason = isPromiseRejection ? protocol::Debugger::Paused::ReasonEnum::PromiseRejection : protocol::Debugger::Paused::ReasonEnum::Exception;
            ErrorString errorString;
            auto obj = injectedScript->wrapObject(&errorString, exception, V8InspectorSession::backtraceObjectGroup);
            m_breakAuxData = obj ? obj->serialize() : nullptr;
            // m_breakAuxData might be null after this.
        }
    }

    OwnPtr<Array<String16>> hitBreakpointIds = Array<String16>::create();

    for (const auto& point : hitBreakpoints) {
        DebugServerBreakpointToBreakpointIdAndSourceMap::iterator breakpointIterator = m_serverBreakpoints.find(point);
        if (breakpointIterator != m_serverBreakpoints.end()) {
            const String16& localId = breakpointIterator->second->first;
            hitBreakpointIds->addItem(localId);

            BreakpointSource source = breakpointIterator->second->second;
            if (m_breakReason == protocol::Debugger::Paused::ReasonEnum::Other && source == DebugCommandBreakpointSource)
                m_breakReason = protocol::Debugger::Paused::ReasonEnum::DebugCommand;
        }
    }

    ErrorString errorString;
    m_frontend->paused(currentCallFrames(&errorString), m_breakReason, m_breakAuxData.release(), hitBreakpointIds.release(), currentAsyncStackTrace());
    m_scheduledDebuggerStep = NoStep;
    m_javaScriptPauseScheduled = false;
    m_steppingFromFramework = false;
    m_pausingOnNativeEvent = false;
    m_skippedStepFrameCount = 0;
    m_recursionLevelForStepFrame = 0;

    if (!m_continueToLocationBreakpointId.isEmpty()) {
        debugger().removeBreakpoint(m_continueToLocationBreakpointId);
        m_continueToLocationBreakpointId = "";
    }
    return result;
}

void V8DebuggerAgentImpl::didContinue()
{
    m_pausedContext.Reset();
    JavaScriptCallFrames emptyCallFrames;
    m_pausedCallFrames.swap(emptyCallFrames);
    clearBreakDetails();
    m_frontend->resumed();
}

void V8DebuggerAgentImpl::breakProgram(const String16& breakReason, PassOwnPtr<protocol::DictionaryValue> data)
{
    if (!enabled() || m_skipAllPauses || !m_pausedContext.IsEmpty() || isCurrentCallStackEmptyOrBlackboxed() || !debugger().breakpointsActivated())
        return;
    m_breakReason = breakReason;
    m_breakAuxData = std::move(data);
    m_scheduledDebuggerStep = NoStep;
    m_steppingFromFramework = false;
    m_pausingOnNativeEvent = false;
    debugger().breakProgram();
}

void V8DebuggerAgentImpl::breakProgramOnException(const String16& breakReason, PassOwnPtr<protocol::DictionaryValue> data)
{
    if (!enabled() || m_debugger->getPauseOnExceptionsState() == V8DebuggerImpl::DontPauseOnExceptions)
        return;
    breakProgram(breakReason, std::move(data));
}

bool V8DebuggerAgentImpl::assertPaused(ErrorString* errorString)
{
    if (m_pausedContext.IsEmpty()) {
        *errorString = "Can only perform operation while paused.";
        return false;
    }
    return true;
}

void V8DebuggerAgentImpl::clearBreakDetails()
{
    m_breakReason = protocol::Debugger::Paused::ReasonEnum::Other;
    m_breakAuxData = nullptr;
}

void V8DebuggerAgentImpl::setBreakpointAt(const String16& scriptId, int lineNumber, int columnNumber, BreakpointSource source, const String16& condition)
{
    String16 breakpointId = generateBreakpointId(scriptId, lineNumber, columnNumber, source);
    ScriptBreakpoint breakpoint(lineNumber, columnNumber, condition);
    resolveBreakpoint(breakpointId, scriptId, breakpoint, source);
}

void V8DebuggerAgentImpl::removeBreakpointAt(const String16& scriptId, int lineNumber, int columnNumber, BreakpointSource source)
{
    removeBreakpoint(generateBreakpointId(scriptId, lineNumber, columnNumber, source));
}

void V8DebuggerAgentImpl::reset()
{
    if (!enabled())
        return;
    m_scheduledDebuggerStep = NoStep;
    m_scripts.clear();
    m_blackboxedPositions.clear();
    m_breakpointIdToDebuggerBreakpointIds.clear();
    allAsyncTasksCanceled();
}

} // namespace blink
