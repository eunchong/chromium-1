/*
 * Copyright (C) 2006, 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2007 Nicholas Shanks <webkit@nickshanks.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "platform/fonts/FontCache.h"

#include "platform/FontFamilyNames.h"

#include "platform/Histogram.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/fonts/AcceptLanguagesResolver.h"
#include "platform/fonts/AlternateFontFamily.h"
#include "platform/fonts/FontCacheClient.h"
#include "platform/fonts/FontCacheKey.h"
#include "platform/fonts/FontDataCache.h"
#include "platform/fonts/FontDescription.h"
#include "platform/fonts/FontPlatformData.h"
#include "platform/fonts/FontSmoothingMode.h"
#include "platform/fonts/SimpleFontData.h"
#include "platform/fonts/TextRenderingMode.h"
#include "platform/fonts/opentype/OpenTypeVerticalData.h"
#include "platform/fonts/shaping/ShapeCache.h"
#include "public/platform/Platform.h"
#include "public/platform/WebMemoryAllocatorDump.h"
#include "public/platform/WebProcessMemoryDump.h"
#include "wtf/HashMap.h"
#include "wtf/ListHashSet.h"
#include "wtf/StdLibExtras.h"
#include "wtf/Vector.h"
#include "wtf/text/AtomicStringHash.h"
#include "wtf/text/StringHash.h"

using namespace WTF;

namespace blink {

#if !OS(WIN) && !OS(LINUX)
FontCache::FontCache()
    : m_purgePreventCount(0)
    , m_fontManager(nullptr)
{
}
#endif // !OS(WIN) && !OS(LINUX)

typedef HashMap<FontCacheKey, OwnPtr<FontPlatformData>, FontCacheKeyHash, FontCacheKeyTraits> FontPlatformDataCache;
typedef HashMap<FallbackListCompositeKey, OwnPtr<ShapeCache>, FallbackListCompositeKeyHash, FallbackListCompositeKeyTraits> FallbackListShaperCache;

static FontPlatformDataCache* gFontPlatformDataCache = nullptr;
static FallbackListShaperCache* gFallbackListShaperCache = nullptr;

SkFontMgr* FontCache::s_fontManager = nullptr;

#if OS(WIN)
bool FontCache::s_antialiasedTextEnabled = false;
bool FontCache::s_lcdTextEnabled = false;
float FontCache::s_deviceScaleFactor = 1.0;
bool FontCache::s_useSkiaFontFallback = false;
#endif // OS(WIN)

FontCache* FontCache::fontCache()
{
    DEFINE_STATIC_LOCAL(FontCache, globalFontCache, ());
    return &globalFontCache;
}

FontPlatformData* FontCache::getFontPlatformData(const FontDescription& fontDescription,
    const FontFaceCreationParams& creationParams, bool checkingAlternateName)
{
    if (!gFontPlatformDataCache) {
        gFontPlatformDataCache = new FontPlatformDataCache;
        platformInit();
    }

    FontCacheKey key = fontDescription.cacheKey(creationParams);
    FontPlatformData* result;
    bool foundResult;
    {
        // addResult's scope must end before we recurse for alternate family names below,
        // to avoid trigering its dtor hash-changed asserts.
        auto addResult = gFontPlatformDataCache->add(key, nullptr);
        if (addResult.isNewEntry)
            addResult.storedValue->value = createFontPlatformData(fontDescription, creationParams, fontDescription.effectiveFontSize());

        result = addResult.storedValue->value.get();
        foundResult = result || !addResult.isNewEntry;
    }

    if (!foundResult && !checkingAlternateName && creationParams.creationType() == CreateFontByFamily) {
        // We were unable to find a font. We have a small set of fonts that we alias to other names,
        // e.g., Arial/Helvetica, Courier/Courier New, etc. Try looking up the font under the aliased name.
        const AtomicString& alternateName = alternateFamilyName(creationParams.family());
        if (!alternateName.isEmpty()) {
            FontFaceCreationParams createByAlternateFamily(alternateName);
            result = getFontPlatformData(fontDescription, createByAlternateFamily, true);
        }
        if (result)
            gFontPlatformDataCache->set(key, adoptPtr(new FontPlatformData(*result))); // Cache the result under the old name.
    }

    return result;
}

ShapeCache* FontCache::getShapeCache(const FallbackListCompositeKey& key)
{
    if (!gFallbackListShaperCache)
        gFallbackListShaperCache = new FallbackListShaperCache;

    FallbackListShaperCache::iterator it = gFallbackListShaperCache->find(key);
    ShapeCache* result = nullptr;
    if (it == gFallbackListShaperCache->end()) {
        result = new ShapeCache();
        gFallbackListShaperCache->set(key, adoptPtr(result));
    } else {
        result = it->value.get();
    }

    ASSERT(result);
    return result;
}

typedef HashMap<FontCache::FontFileKey, RefPtr<OpenTypeVerticalData>, IntHash<FontCache::FontFileKey>, UnsignedWithZeroKeyHashTraits<FontCache::FontFileKey>> FontVerticalDataCache;

FontVerticalDataCache& fontVerticalDataCacheInstance()
{
    DEFINE_STATIC_LOCAL(FontVerticalDataCache, fontVerticalDataCache, ());
    return fontVerticalDataCache;
}

void FontCache::setFontManager(const RefPtr<SkFontMgr>& fontManager)
{
    ASSERT(!s_fontManager);
    s_fontManager = fontManager.get();
    // Explicitly AddRef since we're going to hold on to the object for the life of the program.
    s_fontManager->ref();
}

PassRefPtr<OpenTypeVerticalData> FontCache::getVerticalData(const FontFileKey& key, const FontPlatformData& platformData)
{
    FontVerticalDataCache& fontVerticalDataCache = fontVerticalDataCacheInstance();
    FontVerticalDataCache::iterator result = fontVerticalDataCache.find(key);
    if (result != fontVerticalDataCache.end())
        return result.get()->value;

    RefPtr<OpenTypeVerticalData> verticalData = OpenTypeVerticalData::create(platformData);
    if (!verticalData->isOpenType())
        verticalData.clear();
    fontVerticalDataCache.set(key, verticalData);
    return verticalData;
}

void FontCache::acceptLanguagesChanged(const String& acceptLanguages)
{
    AcceptLanguagesResolver::acceptLanguagesChanged(acceptLanguages);
}

static FontDataCache* gFontDataCache = 0;

PassRefPtr<SimpleFontData> FontCache::getFontData(const FontDescription& fontDescription, const AtomicString& family, bool checkingAlternateName, ShouldRetain shouldRetain)
{
    if (FontPlatformData* platformData = getFontPlatformData(fontDescription, FontFaceCreationParams(adjustFamilyNameToAvoidUnsupportedFonts(family)), checkingAlternateName))
        return fontDataFromFontPlatformData(platformData, shouldRetain);

    return nullptr;
}

PassRefPtr<SimpleFontData> FontCache::fontDataFromFontPlatformData(const FontPlatformData* platformData, ShouldRetain shouldRetain)
{
    if (!gFontDataCache)
        gFontDataCache = new FontDataCache;

#if ENABLE(ASSERT)
    if (shouldRetain == DoNotRetain)
        ASSERT(m_purgePreventCount);
#endif

    return gFontDataCache->get(platformData, shouldRetain);
}

bool FontCache::isPlatformFontAvailable(const FontDescription& fontDescription, const AtomicString& family)
{
    bool checkingAlternateName = true;
    return getFontPlatformData(fontDescription, FontFaceCreationParams(adjustFamilyNameToAvoidUnsupportedFonts(family)), checkingAlternateName);
}

SimpleFontData* FontCache::getNonRetainedLastResortFallbackFont(const FontDescription& fontDescription)
{
    return getLastResortFallbackFont(fontDescription, DoNotRetain).leakRef();
}

void FontCache::releaseFontData(const SimpleFontData* fontData)
{
    ASSERT(gFontDataCache);

    gFontDataCache->release(fontData);
}

static inline void purgePlatformFontDataCache()
{
    if (!gFontPlatformDataCache)
        return;

    Vector<FontCacheKey> keysToRemove;
    keysToRemove.reserveInitialCapacity(gFontPlatformDataCache->size());
    FontPlatformDataCache::iterator platformDataEnd = gFontPlatformDataCache->end();
    for (FontPlatformDataCache::iterator platformData = gFontPlatformDataCache->begin(); platformData != platformDataEnd; ++platformData) {
        if (platformData->value && !gFontDataCache->contains(platformData->value.get()))
            keysToRemove.append(platformData->key);
    }
    gFontPlatformDataCache->removeAll(keysToRemove);
}

static inline void purgeFontVerticalDataCache()
{
    FontVerticalDataCache& fontVerticalDataCache = fontVerticalDataCacheInstance();
    if (!fontVerticalDataCache.isEmpty()) {
        // Mark & sweep unused verticalData
        FontVerticalDataCache::iterator verticalDataEnd = fontVerticalDataCache.end();
        for (FontVerticalDataCache::iterator verticalData = fontVerticalDataCache.begin(); verticalData != verticalDataEnd; ++verticalData) {
            if (verticalData->value)
                verticalData->value->setInFontCache(false);
        }

        gFontDataCache->markAllVerticalData();

        Vector<FontCache::FontFileKey> keysToRemove;
        keysToRemove.reserveInitialCapacity(fontVerticalDataCache.size());
        for (FontVerticalDataCache::iterator verticalData = fontVerticalDataCache.begin(); verticalData != verticalDataEnd; ++verticalData) {
            if (!verticalData->value || !verticalData->value->inFontCache())
                keysToRemove.append(verticalData->key);
        }
        fontVerticalDataCache.removeAll(keysToRemove);
    }
}

static inline void purgeFallbackListShaperCache()
{
    unsigned items = 0;
    if (gFallbackListShaperCache) {
        FallbackListShaperCache::iterator iter;
        for (iter = gFallbackListShaperCache->begin();
            iter != gFallbackListShaperCache->end(); ++iter) {
            items += iter->value->size();
        }
        gFallbackListShaperCache->clear();
    }
    DEFINE_STATIC_LOCAL(CustomCountHistogram, shapeCacheHistogram, ("Blink.Fonts.ShapeCache", 1, 1000000, 50));
    shapeCacheHistogram.count(items);
}

void FontCache::invalidateShapeCache()
{
    purgeFallbackListShaperCache();
}

void FontCache::purge(PurgeSeverity PurgeSeverity)
{
    // We should never be forcing the purge while the FontCachePurgePreventer is in scope.
    ASSERT(!m_purgePreventCount || PurgeSeverity == PurgeIfNeeded);
    if (m_purgePreventCount)
        return;

    if (!gFontDataCache || !gFontDataCache->purge(PurgeSeverity))
        return;

    purgePlatformFontDataCache();
    purgeFontVerticalDataCache();
    purgeFallbackListShaperCache();
}

static bool invalidateFontCache = false;

HeapHashSet<WeakMember<FontCacheClient>>& fontCacheClients()
{
    DEFINE_STATIC_LOCAL(HeapHashSet<WeakMember<FontCacheClient>>, clients, (new HeapHashSet<WeakMember<FontCacheClient>>));
    invalidateFontCache = true;
    return clients;
}

void FontCache::addClient(FontCacheClient* client)
{
    ASSERT(!fontCacheClients().contains(client));
    fontCacheClients().add(client);
}

static unsigned short gGeneration = 0;

unsigned short FontCache::generation()
{
    return gGeneration;
}

void FontCache::invalidate()
{
    if (!invalidateFontCache) {
        ASSERT(!gFontPlatformDataCache);
        return;
    }

    if (gFontPlatformDataCache) {
        delete gFontPlatformDataCache;
        gFontPlatformDataCache = new FontPlatformDataCache;
    }

    gGeneration++;

    HeapVector<Member<FontCacheClient>> clients;
    size_t numClients = fontCacheClients().size();
    clients.reserveInitialCapacity(numClients);
    HeapHashSet<WeakMember<FontCacheClient>>::iterator end = fontCacheClients().end();
    for (HeapHashSet<WeakMember<FontCacheClient>>::iterator it = fontCacheClients().begin(); it != end; ++it)
        clients.append(*it);

    ASSERT(numClients == clients.size());
    for (size_t i = 0; i < numClients; ++i)
        clients[i]->fontCacheInvalidated();

    purge(ForcePurge);
}

void FontCache::dumpFontPlatformDataCache(WebProcessMemoryDump* memoryDump)
{
    ASSERT(isMainThread());
    if (!gFontPlatformDataCache)
        return;
    String dumpName = String("font_caches/font_platform_data_cache");
    WebMemoryAllocatorDump* dump = memoryDump->createMemoryAllocatorDump(dumpName);
    size_t fontPlatformDataObjectsSize = gFontPlatformDataCache->size() * sizeof(FontPlatformData);
    dump->addScalar("size", "bytes", fontPlatformDataObjectsSize);
    memoryDump->addSuballocation(dump->guid(), String(WTF::Partitions::kAllocatedObjectPoolName));
}

void FontCache::dumpShapeResultCache(WebProcessMemoryDump* memoryDump)
{
    ASSERT(isMainThread());
    if (!gFallbackListShaperCache) {
        return;
    }
    String dumpName = String("font_caches/shape_caches");
    WebMemoryAllocatorDump* dump = memoryDump->createMemoryAllocatorDump(dumpName);
    size_t shapeResultCacheSize = 0;
    FallbackListShaperCache::iterator iter;
    for (iter = gFallbackListShaperCache->begin();
        iter != gFallbackListShaperCache->end();
        ++iter) {
        shapeResultCacheSize += iter->value->byteSize();
    }
    dump->addScalar("size", "bytes", shapeResultCacheSize);
    memoryDump->addSuballocation(dump->guid(), String(WTF::Partitions::kAllocatedObjectPoolName));
}

// SkFontMgr requires script-based locale names, like "zh-Hant" and "zh-Hans",
// instead of "zh-CN" and "zh-TW".
CString toSkFontMgrLocale(const String& locale)
{
    if (!locale.startsWith("zh", TextCaseInsensitive))
        return locale.ascii();

    switch (localeToScriptCodeForFontSelection(locale)) {
    case USCRIPT_SIMPLIFIED_HAN:
        return "zh-Hans";
    case USCRIPT_TRADITIONAL_HAN:
        return "zh-Hant";
    default:
        return locale.ascii();
    }
}

} // namespace blink
