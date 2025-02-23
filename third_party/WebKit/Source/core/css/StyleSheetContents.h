/*
 * (C) 1999-2003 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2004, 2006, 2007, 2008, 2009, 2010, 2012 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef StyleSheetContents_h
#define StyleSheetContents_h

#include "core/CoreExport.h"
#include "core/css/RuleSet.h"
#include "platform/heap/Handle.h"
#include "platform/weborigin/KURL.h"
#include "wtf/HashMap.h"
#include "wtf/Vector.h"
#include "wtf/text/AtomicStringHash.h"
#include "wtf/text/StringHash.h"
#include "wtf/text/TextPosition.h"


namespace blink {

class CSSStyleSheet;
class CSSStyleSheetResource;
class Document;
class Node;
class SecurityOrigin;
class StyleRuleBase;
class StyleRuleFontFace;
class StyleRuleImport;
class StyleRuleNamespace;

class CORE_EXPORT StyleSheetContents : public GarbageCollectedFinalized<StyleSheetContents> {
public:
    static StyleSheetContents* create(const CSSParserContext& context)
    {
        return new StyleSheetContents(0, String(), context);
    }
    static StyleSheetContents* create(const String& originalURL, const CSSParserContext& context)
    {
        return new StyleSheetContents(0, originalURL, context);
    }
    static StyleSheetContents* create(StyleRuleImport* ownerRule, const String& originalURL, const CSSParserContext& context)
    {
        return new StyleSheetContents(ownerRule, originalURL, context);
    }

    ~StyleSheetContents();

    const CSSParserContext& parserContext() const { return m_parserContext; }

    const AtomicString& defaultNamespace() { return m_defaultNamespace; }
    const AtomicString& namespaceURIFromPrefix(const AtomicString& prefix);

    void parseAuthorStyleSheet(const CSSStyleSheetResource*, const SecurityOrigin*);
    void parseString(const String&);
    void parseStringAtPosition(const String&, const TextPosition&);

    bool isCacheableForResource() const;
    bool isCacheableForStyleElement() const;

    bool isLoading() const;

    void checkLoaded();
    void startLoadingDynamicSheet();

    StyleSheetContents* rootStyleSheet() const;
    bool hasSingleOwnerNode() const;
    Node* singleOwnerNode() const;
    Document* singleOwnerDocument() const;

    const String& charset() const { return m_parserContext.charset(); }

    bool loadCompleted() const;
    bool hasFailedOrCanceledSubresources() const;

    void setHasSyntacticallyValidCSSHeader(bool isValidCss);
    bool hasSyntacticallyValidCSSHeader() const { return m_hasSyntacticallyValidCSSHeader; }

    void setHasFontFaceRule(bool b) { m_hasFontFaceRule = b; }
    bool hasFontFaceRule() const { return m_hasFontFaceRule; }
    void findFontFaceRules(HeapVector<Member<const StyleRuleFontFace>>& fontFaceRules);

    void parserAddNamespace(const AtomicString& prefix, const AtomicString& uri);
    void parserAppendRule(StyleRuleBase*);

    void clearRules();

    // Rules other than @import.
    const HeapVector<Member<StyleRuleBase>>& childRules() const { return m_childRules; }
    const HeapVector<Member<StyleRuleImport>>& importRules() const { return m_importRules; }
    const HeapVector<Member<StyleRuleNamespace>>& namespaceRules() const { return m_namespaceRules; }

    void notifyLoadedSheet(const CSSStyleSheetResource*);

    StyleSheetContents* parentStyleSheet() const;
    StyleRuleImport* ownerRule() const { return m_ownerRule; }
    void clearOwnerRule() { m_ownerRule = nullptr; }

    // Note that href is the URL that started the redirect chain that led to
    // this style sheet. This property probably isn't useful for much except
    // the JavaScript binding (which needs to use this value for security).
    String originalURL() const { return m_originalURL; }
    const KURL& baseURL() const { return m_parserContext.baseURL(); }

    unsigned ruleCount() const;
    StyleRuleBase* ruleAt(unsigned index) const;

    unsigned estimatedSizeInBytes() const;

    bool wrapperInsertRule(StyleRuleBase*, unsigned index);
    bool wrapperDeleteRule(unsigned index);

    StyleSheetContents* copy() const
    {
        return new StyleSheetContents(*this);
    }

    void registerClient(CSSStyleSheet*);
    void unregisterClient(CSSStyleSheet*);
    size_t clientSize() const { return m_loadingClients.size() + m_completedClients.size(); }
    bool hasOneClient() { return clientSize() == 1; }
    void clientLoadCompleted(CSSStyleSheet*);
    void clientLoadStarted(CSSStyleSheet*);

    bool isMutable() const { return m_isMutable; }
    void setMutable() { m_isMutable = true; }

    void removeSheetFromCache(Document*);

    bool isReferencedFromResource() const { return m_isReferencedFromResource; }
    void setReferencedFromResource(bool);

    void setHasMediaQueries();
    bool hasMediaQueries() const { return m_hasMediaQueries; }

    bool didLoadErrorOccur() const { return m_didLoadErrorOccur; }

    RuleSet& ruleSet() { ASSERT(m_ruleSet); return *m_ruleSet.get(); }
    RuleSet& ensureRuleSet(const MediaQueryEvaluator&, AddRuleFlags);
    void clearRuleSet();

    String sourceMapURL() const { return m_sourceMapURL; }

    DECLARE_TRACE();

private:
    StyleSheetContents(StyleRuleImport* ownerRule, const String& originalURL, const CSSParserContext&);
    StyleSheetContents(const StyleSheetContents&);
    StyleSheetContents() = delete;
    StyleSheetContents& operator=(const StyleSheetContents&) = delete;
    void notifyRemoveFontFaceRule(const StyleRuleFontFace*);

    Document* clientSingleOwnerDocument() const;

    Member<StyleRuleImport> m_ownerRule;

    String m_originalURL;

    HeapVector<Member<StyleRuleImport>> m_importRules;
    HeapVector<Member<StyleRuleNamespace>> m_namespaceRules;
    HeapVector<Member<StyleRuleBase>> m_childRules;
    using PrefixNamespaceURIMap = HashMap<AtomicString, AtomicString>;
    PrefixNamespaceURIMap m_namespaces;
    AtomicString m_defaultNamespace;

    bool m_hasSyntacticallyValidCSSHeader : 1;
    bool m_didLoadErrorOccur : 1;
    bool m_isMutable : 1;
    bool m_isReferencedFromResource : 1;
    bool m_hasFontFaceRule : 1;
    bool m_hasMediaQueries : 1;
    bool m_hasSingleOwnerDocument : 1;

    CSSParserContext m_parserContext;

    HeapHashSet<WeakMember<CSSStyleSheet>> m_loadingClients;
    HeapHashSet<WeakMember<CSSStyleSheet>> m_completedClients;

    Member<RuleSet> m_ruleSet;
    String m_sourceMapURL;
};

} // namespace blink

#endif
