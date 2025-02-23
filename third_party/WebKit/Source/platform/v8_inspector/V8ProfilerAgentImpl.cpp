// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/v8_inspector/V8ProfilerAgentImpl.h"

#include "platform/v8_inspector/Atomics.h"
#include "platform/v8_inspector/V8DebuggerImpl.h"
#include "platform/v8_inspector/V8InspectorSessionImpl.h"
#include "platform/v8_inspector/V8StackTraceImpl.h"
#include "platform/v8_inspector/V8StringUtil.h"
#include <v8-profiler.h>

namespace blink {

namespace ProfilerAgentState {
static const char samplingInterval[] = "samplingInterval";
static const char userInitiatedProfiling[] = "userInitiatedProfiling";
static const char profilerEnabled[] = "profilerEnabled";
}

namespace {

PassOwnPtr<protocol::Array<protocol::Profiler::PositionTickInfo>> buildInspectorObjectForPositionTicks(const v8::CpuProfileNode* node)
{
    OwnPtr<protocol::Array<protocol::Profiler::PositionTickInfo>> array = protocol::Array<protocol::Profiler::PositionTickInfo>::create();
    unsigned lineCount = node->GetHitLineCount();
    if (!lineCount)
        return array.release();

    protocol::Vector<v8::CpuProfileNode::LineTick> entries(lineCount);
    if (node->GetLineTicks(&entries[0], lineCount)) {
        for (unsigned i = 0; i < lineCount; i++) {
            OwnPtr<protocol::Profiler::PositionTickInfo> line = protocol::Profiler::PositionTickInfo::create()
                .setLine(entries[i].line)
                .setTicks(entries[i].hit_count).build();
            array->addItem(line.release());
        }
    }

    return array.release();
}

PassOwnPtr<protocol::Profiler::CPUProfileNode> buildInspectorObjectFor(v8::Isolate* isolate, const v8::CpuProfileNode* node)
{
    v8::HandleScope handleScope(isolate);

    OwnPtr<protocol::Array<protocol::Profiler::CPUProfileNode>> children = protocol::Array<protocol::Profiler::CPUProfileNode>::create();
    const int childrenCount = node->GetChildrenCount();
    for (int i = 0; i < childrenCount; i++) {
        const v8::CpuProfileNode* child = node->GetChild(i);
        children->addItem(buildInspectorObjectFor(isolate, child));
    }

    OwnPtr<protocol::Array<protocol::Profiler::PositionTickInfo>> positionTicks = buildInspectorObjectForPositionTicks(node);

    OwnPtr<protocol::Profiler::CPUProfileNode> result = protocol::Profiler::CPUProfileNode::create()
        .setFunctionName(toProtocolString(node->GetFunctionName()))
        .setScriptId(String16::number(node->GetScriptId()))
        .setUrl(toProtocolString(node->GetScriptResourceName()))
        .setLineNumber(node->GetLineNumber())
        .setColumnNumber(node->GetColumnNumber())
        .setHitCount(node->GetHitCount())
        .setCallUID(node->GetCallUid())
        .setChildren(children.release())
        .setPositionTicks(positionTicks.release())
        .setDeoptReason(node->GetBailoutReason())
        .setId(node->GetNodeId()).build();
    return result.release();
}

PassOwnPtr<protocol::Array<int>> buildInspectorObjectForSamples(v8::CpuProfile* v8profile)
{
    OwnPtr<protocol::Array<int>> array = protocol::Array<int>::create();
    int count = v8profile->GetSamplesCount();
    for (int i = 0; i < count; i++)
        array->addItem(v8profile->GetSample(i)->GetNodeId());
    return array.release();
}

PassOwnPtr<protocol::Array<double>> buildInspectorObjectForTimestamps(v8::CpuProfile* v8profile)
{
    OwnPtr<protocol::Array<double>> array = protocol::Array<double>::create();
    int count = v8profile->GetSamplesCount();
    for (int i = 0; i < count; i++)
        array->addItem(v8profile->GetSampleTimestamp(i));
    return array.release();
}

PassOwnPtr<protocol::Profiler::CPUProfile> createCPUProfile(v8::Isolate* isolate, v8::CpuProfile* v8profile)
{
    OwnPtr<protocol::Profiler::CPUProfile> profile = protocol::Profiler::CPUProfile::create()
        .setHead(buildInspectorObjectFor(isolate, v8profile->GetTopDownRoot()))
        .setStartTime(static_cast<double>(v8profile->GetStartTime()) / 1000000)
        .setEndTime(static_cast<double>(v8profile->GetEndTime()) / 1000000).build();
    profile->setSamples(buildInspectorObjectForSamples(v8profile));
    profile->setTimestamps(buildInspectorObjectForTimestamps(v8profile));
    return profile.release();
}

PassOwnPtr<protocol::Debugger::Location> currentDebugLocation(V8DebuggerImpl* debugger)
{
    OwnPtr<V8StackTrace> callStack = debugger->captureStackTrace(1);
    OwnPtr<protocol::Debugger::Location> location = protocol::Debugger::Location::create()
        .setScriptId(callStack->topScriptId())
        .setLineNumber(callStack->topLineNumber()).build();
    location->setColumnNumber(callStack->topColumnNumber());
    return location.release();
}

volatile int s_lastProfileId = 0;

} // namespace

class V8ProfilerAgentImpl::ProfileDescriptor {
public:
    ProfileDescriptor(const String16& id, const String16& title)
        : m_id(id)
        , m_title(title) { }
    String16 m_id;
    String16 m_title;
};

V8ProfilerAgentImpl::V8ProfilerAgentImpl(V8InspectorSessionImpl* session)
    : m_session(session)
    , m_isolate(m_session->debugger()->isolate())
    , m_state(nullptr)
    , m_frontend(nullptr)
    , m_enabled(false)
    , m_recordingCPUProfile(false)
{
}

V8ProfilerAgentImpl::~V8ProfilerAgentImpl()
{
}

void V8ProfilerAgentImpl::consoleProfile(const String16& title)
{
    if (!m_enabled)
        return;
    ASSERT(m_frontend);
    String16 id = nextProfileId();
    m_startedProfiles.append(ProfileDescriptor(id, title));
    startProfiling(id);
    m_frontend->consoleProfileStarted(id, currentDebugLocation(m_session->debugger()), title);
}

void V8ProfilerAgentImpl::consoleProfileEnd(const String16& title)
{
    if (!m_enabled)
        return;
    ASSERT(m_frontend);
    String16 id;
    String16 resolvedTitle;
    // Take last started profile if no title was passed.
    if (title.isEmpty()) {
        if (m_startedProfiles.isEmpty())
            return;
        id = m_startedProfiles.last().m_id;
        resolvedTitle = m_startedProfiles.last().m_title;
        m_startedProfiles.removeLast();
    } else {
        for (size_t i = 0; i < m_startedProfiles.size(); i++) {
            if (m_startedProfiles[i].m_title == title) {
                resolvedTitle = title;
                id = m_startedProfiles[i].m_id;
                m_startedProfiles.remove(i);
                break;
            }
        }
        if (id.isEmpty())
            return;
    }
    OwnPtr<protocol::Profiler::CPUProfile> profile = stopProfiling(id, true);
    if (!profile)
        return;
    OwnPtr<protocol::Debugger::Location> location = currentDebugLocation(m_session->debugger());
    m_frontend->consoleProfileFinished(id, location.release(), profile.release(), resolvedTitle);
}

void V8ProfilerAgentImpl::enable(ErrorString*)
{
    if (m_enabled)
        return;
    m_enabled = true;
    m_state->setBoolean(ProfilerAgentState::profilerEnabled, true);
    m_session->changeInstrumentationCounter(+1);
}

void V8ProfilerAgentImpl::disable(ErrorString* errorString)
{
    if (!m_enabled)
        return;
    m_session->changeInstrumentationCounter(-1);
    for (size_t i = m_startedProfiles.size(); i > 0; --i)
        stopProfiling(m_startedProfiles[i - 1].m_id, false);
    m_startedProfiles.clear();
    stop(nullptr, nullptr);
    m_enabled = false;
    m_state->setBoolean(ProfilerAgentState::profilerEnabled, false);
}

void V8ProfilerAgentImpl::setSamplingInterval(ErrorString* error, int interval)
{
    if (m_recordingCPUProfile) {
        *error = "Cannot change sampling interval when profiling.";
        return;
    }
    m_state->setNumber(ProfilerAgentState::samplingInterval, interval);
    m_isolate->GetCpuProfiler()->SetSamplingInterval(interval);
}

void V8ProfilerAgentImpl::clearFrontend()
{
    ErrorString error;
    disable(&error);
    ASSERT(m_frontend);
    m_frontend = nullptr;
}

void V8ProfilerAgentImpl::restore()
{
    ASSERT(!m_enabled);
    if (!m_state->booleanProperty(ProfilerAgentState::profilerEnabled, false))
        return;
    m_enabled = true;
    m_session->changeInstrumentationCounter(+1);
    int interval = 0;
    m_state->getNumber(ProfilerAgentState::samplingInterval, &interval);
    if (interval)
        m_isolate->GetCpuProfiler()->SetSamplingInterval(interval);
    if (m_state->booleanProperty(ProfilerAgentState::userInitiatedProfiling, false)) {
        ErrorString error;
        start(&error);
    }
}

void V8ProfilerAgentImpl::start(ErrorString* error)
{
    if (m_recordingCPUProfile)
        return;
    if (!m_enabled) {
        *error = "Profiler is not enabled";
        return;
    }
    m_recordingCPUProfile = true;
    m_frontendInitiatedProfileId = nextProfileId();
    startProfiling(m_frontendInitiatedProfileId);
    m_state->setBoolean(ProfilerAgentState::userInitiatedProfiling, true);
    m_session->client()->profilingStarted();
}

void V8ProfilerAgentImpl::stop(ErrorString* errorString, OwnPtr<protocol::Profiler::CPUProfile>* profile)
{
    if (!m_recordingCPUProfile) {
        if (errorString)
            *errorString = "No recording profiles found";
        return;
    }
    m_recordingCPUProfile = false;
    OwnPtr<protocol::Profiler::CPUProfile> cpuProfile = stopProfiling(m_frontendInitiatedProfileId, !!profile);
    if (profile) {
        *profile = cpuProfile.release();
        if (!profile->get() && errorString)
            *errorString = "Profile is not found";
    }
    m_frontendInitiatedProfileId = String16();
    m_state->setBoolean(ProfilerAgentState::userInitiatedProfiling, false);
    m_session->client()->profilingStopped();
}

String16 V8ProfilerAgentImpl::nextProfileId()
{
    return String16::number(atomicIncrement(&s_lastProfileId));
}

void V8ProfilerAgentImpl::startProfiling(const String16& title)
{
    v8::HandleScope handleScope(m_isolate);
    m_isolate->GetCpuProfiler()->StartProfiling(toV8String(m_isolate, title), true);
}

PassOwnPtr<protocol::Profiler::CPUProfile> V8ProfilerAgentImpl::stopProfiling(const String16& title, bool serialize)
{
    v8::HandleScope handleScope(m_isolate);
    v8::CpuProfile* profile = m_isolate->GetCpuProfiler()->StopProfiling(toV8String(m_isolate, title));
    if (!profile)
        return nullptr;
    OwnPtr<protocol::Profiler::CPUProfile> result;
    if (serialize)
        result = createCPUProfile(m_isolate, profile);
    profile->Delete();
    return result.release();
}

bool V8ProfilerAgentImpl::isRecording() const
{
    return m_recordingCPUProfile || !m_startedProfiles.isEmpty();
}

} // namespace blink
