// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/ntp/most_visited_sites.h"

#include <utility>

#include "base/callback.h"
#include "base/command_line.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram.h"
#include "base/metrics/sparse_histogram.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "components/history/core/browser/top_sites.h"
#include "components/ntp_tiles/pref_names.h"
#include "components/ntp_tiles/switches.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/variations/variations_associated_data.h"
#include "content/public/browser/browser_thread.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "url/gurl.h"

using content::BrowserThread;
using history::TopSites;
using suggestions::ChromeSuggestion;
using suggestions::SuggestionsProfile;
using suggestions::SuggestionsService;

namespace {

// Identifiers for the various tile sources.
const char kHistogramClientName[] = "client";
const char kHistogramServerName[] = "server";
const char kHistogramServerFormat[] = "server%d";
const char kHistogramPopularName[] = "popular";
const char kHistogramWhitelistName[] = "whitelist";

const char kPopularSitesFieldTrialName[] = "NTPPopularSites";

// The visual type of a most visited tile.
//
// These values must stay in sync with the MostVisitedTileType enum
// in histograms.xml.
//
// A Java counterpart will be generated for this enum.
// GENERATED_JAVA_ENUM_PACKAGE: org.chromium.chrome.browser.ntp
enum MostVisitedTileType {
    // The icon or thumbnail hasn't loaded yet.
    NONE,
    // The item displays a site's actual favicon or touch icon.
    ICON_REAL,
    // The item displays a color derived from the site's favicon or touch icon.
    ICON_COLOR,
    // The item displays a default gray box in place of an icon.
    ICON_DEFAULT,
    NUM_TILE_TYPES,
};

std::unique_ptr<SkBitmap> MaybeFetchLocalThumbnail(
    const GURL& url,
    const scoped_refptr<TopSites>& top_sites) {
  DCHECK_CURRENTLY_ON(BrowserThread::DB);
  scoped_refptr<base::RefCountedMemory> image;
  std::unique_ptr<SkBitmap> bitmap;
  if (top_sites && top_sites->GetPageThumbnail(url, false, &image))
    bitmap = gfx::JPEGCodec::Decode(image->front(), image->size());
  return bitmap;
}

// Log an event for a given |histogram| at a given element |position|. This
// routine exists because regular histogram macros are cached thus can't be used
// if the name of the histogram will change at a given call site.
void LogHistogramEvent(const std::string& histogram,
                       int position,
                       int num_sites) {
  base::HistogramBase* counter = base::LinearHistogram::FactoryGet(
      histogram,
      1,
      num_sites,
      num_sites + 1,
      base::Histogram::kUmaTargetedHistogramFlag);
  if (counter)
    counter->Add(position);
}

bool ShouldShowPopularSites() {
  // Note: It's important to query the field trial state first, to ensure that
  // UMA reports the correct group.
  const std::string group_name =
      base::FieldTrialList::FindFullName(kPopularSitesFieldTrialName);
  base::CommandLine* cmd_line = base::CommandLine::ForCurrentProcess();
  if (cmd_line->HasSwitch(ntp_tiles::switches::kDisableNTPPopularSites))
    return false;
  if (cmd_line->HasSwitch(ntp_tiles::switches::kEnableNTPPopularSites))
    return true;
  return base::StartsWith(group_name, "Enabled",
                          base::CompareCase::INSENSITIVE_ASCII);
}

std::string GetPopularSitesCountry() {
  return variations::GetVariationParamValue(kPopularSitesFieldTrialName,
                                            "country");
}

std::string GetPopularSitesVersion() {
  return variations::GetVariationParamValue(kPopularSitesFieldTrialName,
                                            "version");
}

// Determine whether we need any popular suggestions to fill up a grid of
// |num_tiles| tiles.
bool NeedPopularSites(const PrefService* prefs, size_t num_tiles) {
  const base::ListValue* source_list =
      prefs->GetList(ntp_tiles::prefs::kNTPSuggestionsIsPersonal);
  // If there aren't enough previous suggestions to fill the grid, we need
  // popular suggestions.
  if (source_list->GetSize() < num_tiles)
    return true;
  // Otherwise, if any of the previous suggestions is not personal, then also
  // get popular suggestions.
  for (size_t i = 0; i < num_tiles; ++i) {
    bool is_personal = false;
    if (source_list->GetBoolean(i, &is_personal) && !is_personal)
      return true;
  }
  // The whole grid is already filled with personal suggestions, no point in
  // bothering with popular ones.
  return false;
}

bool AreURLsEquivalent(const GURL& url1, const GURL& url2) {
  return url1.host() == url2.host() && url1.path() == url2.path();
}

std::string GetSourceHistogramName(
        const MostVisitedSites::Suggestion& suggestion) {
  switch (suggestion.source) {
    case MostVisitedSites::TOP_SITES:
      return kHistogramClientName;
    case MostVisitedSites::POPULAR:
      return kHistogramPopularName;
    case MostVisitedSites::WHITELIST:
      return kHistogramWhitelistName;
    case MostVisitedSites::SUGGESTIONS_SERVICE:
      return suggestion.provider_index >= 0
                 ? base::StringPrintf(kHistogramServerFormat,
                                      suggestion.provider_index)
                 : kHistogramServerName;
  }
  NOTREACHED();
  return std::string();
}

}  // namespace

MostVisitedSites::Suggestion::Suggestion() : provider_index(-1) {}

MostVisitedSites::Suggestion::~Suggestion() {}

MostVisitedSites::Suggestion::Suggestion(Suggestion&&) = default;
MostVisitedSites::Suggestion&
MostVisitedSites::Suggestion::operator=(Suggestion&&) = default;

MostVisitedSites::MostVisitedSites(
    PrefService* prefs,
    const TemplateURLService* template_url_service,
    variations::VariationsService* variations_service,
    net::URLRequestContextGetter* download_context,
    const base::FilePath& popular_sites_directory,
    scoped_refptr<history::TopSites> top_sites,
    SuggestionsService* suggestions,
    MostVisitedSitesSupervisor* supervisor)
    : prefs_(prefs),
      template_url_service_(template_url_service),
      variations_service_(variations_service),
      download_context_(download_context),
      popular_sites_directory_(popular_sites_directory),
      top_sites_(top_sites),
      suggestions_service_(suggestions),
      supervisor_(supervisor),
      observer_(nullptr),
      num_sites_(0),
      received_most_visited_sites_(false),
      received_popular_sites_(false),
      recorded_uma_(false),
      scoped_observer_(this),
      mv_source_(SUGGESTIONS_SERVICE),
      weak_ptr_factory_(this) {
  supervisor_->SetObserver(this);
}

MostVisitedSites::~MostVisitedSites() {
  supervisor_->SetObserver(nullptr);
}

void MostVisitedSites::SetMostVisitedURLsObserver(
      MostVisitedSites::Observer* observer, int num_sites) {
  DCHECK(observer);
  observer_ = observer;
  num_sites_ = num_sites;

  if (ShouldShowPopularSites() &&
      NeedPopularSites(prefs_, num_sites_)) {
    popular_sites_.reset(new PopularSites(
        prefs_, template_url_service_, variations_service_, download_context_,
        popular_sites_directory_, GetPopularSitesCountry(),
        GetPopularSitesVersion(), false,
        base::Bind(&MostVisitedSites::OnPopularSitesAvailable,
                   base::Unretained(this))));
  } else {
    received_popular_sites_ = true;
  }

  if (top_sites_) {
    // TopSites updates itself after a delay. To ensure up-to-date results,
    // force an update now.
    top_sites_->SyncWithHistory();

    // Register as TopSitesObserver so that we can update ourselves when the
    // TopSites changes.
    scoped_observer_.Add(top_sites_.get());
  }

  suggestions_subscription_ = suggestions_service_->AddCallback(
      base::Bind(&MostVisitedSites::OnSuggestionsProfileAvailable,
                 base::Unretained(this)));

  // Immediately build the current suggestions, getting personal suggestions
  // from the SuggestionsService's cache or, if that is empty, from TopSites.
  BuildCurrentSuggestions();
  // Also start a request for fresh suggestions.
  suggestions_service_->FetchSuggestionsData();
}

void MostVisitedSites::GetURLThumbnail(
    const GURL& url,
    const ThumbnailCallback& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  BrowserThread::PostTaskAndReplyWithResult(
      BrowserThread::DB, FROM_HERE,
      base::Bind(&MaybeFetchLocalThumbnail, url, top_sites_),
      base::Bind(&MostVisitedSites::OnLocalThumbnailFetched,
                 weak_ptr_factory_.GetWeakPtr(), url, callback));
}

void MostVisitedSites::OnLocalThumbnailFetched(
    const GURL& url,
    const ThumbnailCallback& callback,
    std::unique_ptr<SkBitmap> bitmap) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (!bitmap.get()) {
    // A thumbnail is not locally available for |url|. Make sure it is put in
    // the list to be fetched at the next visit to this site.
    if (top_sites_)
      top_sites_->AddForcedURL(url, base::Time::Now());
    // Also fetch a remote thumbnail if possible. PopularSites or the
    // SuggestionsService can supply a thumbnail download URL.
    if (popular_sites_) {
      const std::vector<PopularSites::Site>& sites = popular_sites_->sites();
      auto it = std::find_if(
          sites.begin(), sites.end(),
          [&url](const PopularSites::Site& site) { return site.url == url; });
      if (it != sites.end() && it->thumbnail_url.is_valid()) {
        return suggestions_service_->GetPageThumbnailWithURL(
            url, it->thumbnail_url,
            base::Bind(&MostVisitedSites::OnObtainedThumbnail,
                       weak_ptr_factory_.GetWeakPtr(), false, callback));
      }
    }
    if (mv_source_ == SUGGESTIONS_SERVICE) {
      return suggestions_service_->GetPageThumbnail(
          url, base::Bind(&MostVisitedSites::OnObtainedThumbnail,
                          weak_ptr_factory_.GetWeakPtr(), false, callback));
    }
  }
  OnObtainedThumbnail(true, callback, url, bitmap.get());
}

void MostVisitedSites::OnObtainedThumbnail(
    bool is_local_thumbnail,
    const ThumbnailCallback& callback,
    const GURL& url,
    const SkBitmap* bitmap) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  callback.Run(is_local_thumbnail, bitmap);
}

void MostVisitedSites::AddOrRemoveBlacklistedUrl(
    const GURL& url, bool add_url) {
  // Always blacklist in the local TopSites.
  if (top_sites_) {
    if (add_url)
      top_sites_->AddBlacklistedURL(url);
    else
      top_sites_->RemoveBlacklistedURL(url);
  }

  // Only blacklist in the server-side suggestions service if it's active.
  if (mv_source_ == SUGGESTIONS_SERVICE) {
    if (add_url)
      suggestions_service_->BlacklistURL(url);
    else
      suggestions_service_->UndoBlacklistURL(url);
  }
}

void MostVisitedSites::RecordTileTypeMetrics(
    const std::vector<int>& tile_types) {
  DCHECK_EQ(current_suggestions_.size(), tile_types.size());
  int counts_per_type[NUM_TILE_TYPES] = {0};
  for (size_t i = 0; i < tile_types.size(); ++i) {
    int tile_type = tile_types[i];
    ++counts_per_type[tile_type];
    std::string histogram = base::StringPrintf(
        "NewTabPage.TileType.%s",
        GetSourceHistogramName(current_suggestions_[i]).c_str());
    LogHistogramEvent(histogram, tile_type, NUM_TILE_TYPES);
  }

  UMA_HISTOGRAM_SPARSE_SLOWLY("NewTabPage.IconsReal",
                              counts_per_type[ICON_REAL]);
  UMA_HISTOGRAM_SPARSE_SLOWLY("NewTabPage.IconsColor",
                              counts_per_type[ICON_COLOR]);
  UMA_HISTOGRAM_SPARSE_SLOWLY("NewTabPage.IconsGray",
                              counts_per_type[ICON_DEFAULT]);
}

void MostVisitedSites::RecordOpenedMostVisitedItem(int index, int tile_type) {
  DCHECK_GE(index, 0);
  DCHECK_LT(index, static_cast<int>(current_suggestions_.size()));
  std::string histogram = base::StringPrintf(
      "NewTabPage.MostVisited.%s",
      GetSourceHistogramName(current_suggestions_[index]).c_str());
  LogHistogramEvent(histogram, index, num_sites_);

  histogram = base::StringPrintf(
      "NewTabPage.TileTypeClicked.%s",
      GetSourceHistogramName(current_suggestions_[index]).c_str());
  LogHistogramEvent(histogram, tile_type, NUM_TILE_TYPES);
}

void MostVisitedSites::OnBlockedSitesChanged() {
  BuildCurrentSuggestions();
}

// static
void MostVisitedSites::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterListPref(ntp_tiles::prefs::kNTPSuggestionsURL);
  registry->RegisterListPref(ntp_tiles::prefs::kNTPSuggestionsIsPersonal);
}

void MostVisitedSites::BuildCurrentSuggestions() {
  // Get the current suggestions from cache. If the cache is empty, this will
  // fall back to TopSites.
  OnSuggestionsProfileAvailable(
      suggestions_service_->GetSuggestionsDataFromCache());
}

void MostVisitedSites::InitiateTopSitesQuery() {
  if (!top_sites_)
    return;

  top_sites_->GetMostVisitedURLs(
      base::Bind(&MostVisitedSites::OnMostVisitedURLsAvailable,
                 weak_ptr_factory_.GetWeakPtr()),
      false);
}

base::FilePath MostVisitedSites::GetWhitelistLargeIconPath(const GURL& url) {
  for (const auto& whitelist : supervisor_->whitelists()) {
    if (AreURLsEquivalent(whitelist.entry_point, url))
      return whitelist.large_icon_path;
  }
  return base::FilePath();
}

void MostVisitedSites::OnMostVisitedURLsAvailable(
    const history::MostVisitedURLList& visited_list) {
  MostVisitedSites::SuggestionsPtrVector suggestions;
  size_t num_tiles =
      std::min(visited_list.size(), static_cast<size_t>(num_sites_));
  for (size_t i = 0; i < num_tiles; ++i) {
    const history::MostVisitedURL& visited = visited_list[i];
    if (visited.url.is_empty()) {
      num_tiles = i;
      break;  // This is the signal that there are no more real visited sites.
    }
    if (supervisor_->IsBlocked(visited.url))
      continue;

    std::unique_ptr<Suggestion> suggestion(new Suggestion());
    suggestion->title = visited.title;
    suggestion->url = visited.url;
    suggestion->source = TOP_SITES;
    suggestion->whitelist_icon_path = GetWhitelistLargeIconPath(visited.url);

    suggestions.push_back(std::move(suggestion));
  }

  received_most_visited_sites_ = true;
  mv_source_ = TOP_SITES;
  SaveNewNTPSuggestions(&suggestions);
  NotifyMostVisitedURLsObserver();
}

void MostVisitedSites::OnSuggestionsProfileAvailable(
    const SuggestionsProfile& suggestions_profile) {
  int num_tiles = suggestions_profile.suggestions_size();
  // With no server suggestions, fall back to local TopSites.
  if (num_tiles == 0) {
    InitiateTopSitesQuery();
    return;
  }
  if (num_sites_ < num_tiles)
    num_tiles = num_sites_;

  MostVisitedSites::SuggestionsPtrVector suggestions;
  for (int i = 0; i < num_tiles; ++i) {
    const ChromeSuggestion& suggestion = suggestions_profile.suggestions(i);
    if (supervisor_->IsBlocked(GURL(suggestion.url())))
      continue;

    std::unique_ptr<Suggestion> generated_suggestion(new Suggestion());
    generated_suggestion->title = base::UTF8ToUTF16(suggestion.title());
    generated_suggestion->url = GURL(suggestion.url());
    generated_suggestion->source = SUGGESTIONS_SERVICE;
    generated_suggestion->whitelist_icon_path = GetWhitelistLargeIconPath(
        GURL(suggestion.url()));
    if (suggestion.providers_size() > 0)
      generated_suggestion->provider_index = suggestion.providers(0);

    suggestions.push_back(std::move(generated_suggestion));
  }

  received_most_visited_sites_ = true;
  mv_source_ = SUGGESTIONS_SERVICE;
  SaveNewNTPSuggestions(&suggestions);
  NotifyMostVisitedURLsObserver();
}

MostVisitedSites::SuggestionsPtrVector
MostVisitedSites::CreateWhitelistEntryPointSuggestions(
    const MostVisitedSites::SuggestionsPtrVector& personal_suggestions) {
  size_t num_personal_suggestions = personal_suggestions.size();
  DCHECK_LE(num_personal_suggestions, static_cast<size_t>(num_sites_));

  size_t num_whitelist_suggestions = num_sites_ - num_personal_suggestions;
  MostVisitedSites::SuggestionsPtrVector whitelist_suggestions;

  std::set<std::string> personal_hosts;
  for (const auto& suggestion : personal_suggestions)
    personal_hosts.insert(suggestion->url.host());

  for (const auto& whitelist : supervisor_->whitelists()) {
    // Skip blacklisted sites.
    if (top_sites_ && top_sites_->IsBlacklisted(whitelist.entry_point))
      continue;

    // Skip suggestions already present.
    if (personal_hosts.find(whitelist.entry_point.host()) !=
        personal_hosts.end())
      continue;

    // Skip whitelist entry points that are manually blocked.
    if (supervisor_->IsBlocked(whitelist.entry_point))
      continue;

    std::unique_ptr<Suggestion> suggestion(new Suggestion());
    suggestion->title = whitelist.title;
    suggestion->url = whitelist.entry_point;
    suggestion->source = WHITELIST;
    suggestion->whitelist_icon_path = whitelist.large_icon_path;

    whitelist_suggestions.push_back(std::move(suggestion));
    if (whitelist_suggestions.size() >= num_whitelist_suggestions)
      break;
  }

  return whitelist_suggestions;
}

MostVisitedSites::SuggestionsPtrVector
MostVisitedSites::CreatePopularSitesSuggestions(
    const MostVisitedSites::SuggestionsPtrVector& personal_suggestions,
    const MostVisitedSites::SuggestionsPtrVector& whitelist_suggestions) {
  // For child accounts popular sites suggestions will not be added.
  if (supervisor_->IsChildProfile())
    return MostVisitedSites::SuggestionsPtrVector();

  size_t num_suggestions =
      personal_suggestions.size() + whitelist_suggestions.size();
  DCHECK_LE(num_suggestions, static_cast<size_t>(num_sites_));

  // Collect non-blacklisted popular suggestions, skipping those already present
  // in the personal suggestions.
  size_t num_popular_sites_suggestions = num_sites_ - num_suggestions;
  MostVisitedSites::SuggestionsPtrVector popular_sites_suggestions;

  if (num_popular_sites_suggestions > 0 && popular_sites_) {
    std::set<std::string> hosts;
    for (const auto& suggestion : personal_suggestions)
      hosts.insert(suggestion->url.host());
    for (const auto& suggestion : whitelist_suggestions)
      hosts.insert(suggestion->url.host());
    for (const PopularSites::Site& popular_site : popular_sites_->sites()) {
      // Skip blacklisted sites.
      if (top_sites_ && top_sites_->IsBlacklisted(popular_site.url))
        continue;
      std::string host = popular_site.url.host();
      // Skip suggestions already present in personal or whitelists.
      if (hosts.find(host) != hosts.end())
        continue;

      std::unique_ptr<Suggestion> suggestion(new Suggestion());
      suggestion->title = popular_site.title;
      suggestion->url = GURL(popular_site.url);
      suggestion->source = POPULAR;

      popular_sites_suggestions.push_back(std::move(suggestion));
      if (popular_sites_suggestions.size() >= num_popular_sites_suggestions)
        break;
    }
  }
  return popular_sites_suggestions;
}

void MostVisitedSites::SaveNewNTPSuggestions(
    MostVisitedSites::SuggestionsPtrVector* personal_suggestions) {
  MostVisitedSites::SuggestionsPtrVector whitelist_suggestions =
      CreateWhitelistEntryPointSuggestions(*personal_suggestions);
  MostVisitedSites::SuggestionsPtrVector popular_sites_suggestions =
      CreatePopularSitesSuggestions(*personal_suggestions,
                                    whitelist_suggestions);
  size_t num_actual_tiles = personal_suggestions->size() +
                            whitelist_suggestions.size() +
                            popular_sites_suggestions.size();
  std::vector<std::string> old_sites_url;
  std::vector<bool> old_sites_is_personal;
  // TODO(treib): We used to call |GetPreviousNTPSites| here to populate
  // |old_sites_url| and |old_sites_is_personal|, but that caused problems
  // (crbug.com/585391). Either figure out a way to fix them and re-enable,
  // or properly remove the order-persisting code. crbug.com/601734
  MostVisitedSites::SuggestionsPtrVector merged_suggestions = MergeSuggestions(
      personal_suggestions, &whitelist_suggestions, &popular_sites_suggestions,
      old_sites_url, old_sites_is_personal);
  DCHECK_EQ(num_actual_tiles, merged_suggestions.size());
  current_suggestions_.resize(merged_suggestions.size());
  for (size_t i = 0; i < merged_suggestions.size(); ++i)
    std::swap(*merged_suggestions[i], current_suggestions_[i]);
  if (received_popular_sites_)
    SaveCurrentNTPSites();
}

// static
MostVisitedSites::SuggestionsPtrVector MostVisitedSites::MergeSuggestions(
    MostVisitedSites::SuggestionsPtrVector* personal_suggestions,
    MostVisitedSites::SuggestionsPtrVector* whitelist_suggestions,
    MostVisitedSites::SuggestionsPtrVector* popular_suggestions,
    const std::vector<std::string>& old_sites_url,
    const std::vector<bool>& old_sites_is_personal) {
  size_t num_personal_suggestions = personal_suggestions->size();
  size_t num_whitelist_suggestions = whitelist_suggestions->size();
  size_t num_popular_suggestions = popular_suggestions->size();
  size_t num_tiles = num_popular_suggestions + num_whitelist_suggestions +
                     num_personal_suggestions;
  MostVisitedSites::SuggestionsPtrVector merged_suggestions;
  merged_suggestions.resize(num_tiles);

  size_t num_old_tiles = old_sites_url.size();
  DCHECK_LE(num_old_tiles, num_tiles);
  DCHECK_EQ(num_old_tiles, old_sites_is_personal.size());
  std::vector<std::string> old_sites_host;
  old_sites_host.reserve(num_old_tiles);
  // Only populate the hosts for popular suggestions as only they can be
  // replaced by host. Personal suggestions require an exact url match to be
  // replaced.
  for (size_t i = 0; i < num_old_tiles; ++i) {
    old_sites_host.push_back(old_sites_is_personal[i]
                                 ? std::string()
                                 : GURL(old_sites_url[i]).host());
  }

  // Insert personal suggestions if they existed previously.
  std::vector<size_t> new_personal_suggestions = InsertMatchingSuggestions(
      personal_suggestions, &merged_suggestions, old_sites_url, old_sites_host);
  // Insert whitelist suggestions if they existed previously.
  std::vector<size_t> new_whitelist_suggestions =
      InsertMatchingSuggestions(whitelist_suggestions, &merged_suggestions,
                                old_sites_url, old_sites_host);
  // Insert popular suggestions if they existed previously.
  std::vector<size_t> new_popular_suggestions = InsertMatchingSuggestions(
      popular_suggestions, &merged_suggestions, old_sites_url, old_sites_host);
  // Insert leftover personal suggestions.
  size_t filled_so_far = InsertAllSuggestions(
      0, new_personal_suggestions, personal_suggestions, &merged_suggestions);
  // Insert leftover whitelist suggestions.
  filled_so_far =
      InsertAllSuggestions(filled_so_far, new_whitelist_suggestions,
                           whitelist_suggestions, &merged_suggestions);
  // Insert leftover popular suggestions.
  InsertAllSuggestions(filled_so_far, new_popular_suggestions,
                       popular_suggestions, &merged_suggestions);
  return merged_suggestions;
}

void MostVisitedSites::GetPreviousNTPSites(
    size_t num_tiles,
    std::vector<std::string>* old_sites_url,
    std::vector<bool>* old_sites_is_personal) const {
  const base::ListValue* url_list = prefs_->GetList(
      ntp_tiles::prefs::kNTPSuggestionsURL);
  const base::ListValue* source_list =
      prefs_->GetList(ntp_tiles::prefs::kNTPSuggestionsIsPersonal);
  DCHECK_EQ(url_list->GetSize(), source_list->GetSize());
  if (url_list->GetSize() < num_tiles)
    num_tiles = url_list->GetSize();
  if (num_tiles == 0) {
    // No fallback required as Personal suggestions take precedence anyway.
    return;
  }
  old_sites_url->reserve(num_tiles);
  old_sites_is_personal->reserve(num_tiles);
  for (size_t i = 0; i < num_tiles; ++i) {
    std::string url_string;
    bool success = url_list->GetString(i, &url_string);
    DCHECK(success);
    old_sites_url->push_back(url_string);
    bool is_personal;
    success = source_list->GetBoolean(i, &is_personal);
    DCHECK(success);
    old_sites_is_personal->push_back(is_personal);
  }
}

void MostVisitedSites::SaveCurrentNTPSites() {
  base::ListValue url_list;
  base::ListValue source_list;
  for (const auto& suggestion : current_suggestions_) {
    url_list.AppendString(suggestion.url.spec());
    source_list.AppendBoolean(suggestion.source != MostVisitedSites::POPULAR);
  }
  prefs_->Set(ntp_tiles::prefs::kNTPSuggestionsIsPersonal, source_list);
  prefs_->Set(ntp_tiles::prefs::kNTPSuggestionsURL, url_list);
}

// static
std::vector<size_t> MostVisitedSites::InsertMatchingSuggestions(
    MostVisitedSites::SuggestionsPtrVector* src_suggestions,
    MostVisitedSites::SuggestionsPtrVector* dst_suggestions,
    const std::vector<std::string>& match_urls,
    const std::vector<std::string>& match_hosts) {
  std::vector<size_t> unmatched_suggestions;
  size_t num_src_suggestions = src_suggestions->size();
  size_t num_matchers = match_urls.size();
  for (size_t i = 0; i < num_src_suggestions; ++i) {
    size_t position;
    for (position = 0; position < num_matchers; ++position) {
      if ((*dst_suggestions)[position] != nullptr)
        continue;
      if (match_urls[position] == (*src_suggestions)[i]->url.spec())
        break;
      // match_hosts is only populated for suggestions which can be replaced by
      // host matching like popular suggestions.
      if (match_hosts[position] == (*src_suggestions)[i]->url.host())
        break;
    }
    if (position == num_matchers) {
      unmatched_suggestions.push_back(i);
    } else {
      // A move is required as the source and destination containers own the
      // elements.
      std::swap((*dst_suggestions)[position], (*src_suggestions)[i]);
    }
  }
  return unmatched_suggestions;
}

// static
size_t MostVisitedSites::InsertAllSuggestions(
    size_t start_position,
    const std::vector<size_t>& insert_positions,
    std::vector<std::unique_ptr<Suggestion>>* src_suggestions,
    std::vector<std::unique_ptr<Suggestion>>* dst_suggestions) {
  size_t num_inserts = insert_positions.size();
  size_t num_dests = dst_suggestions->size();

  size_t src_pos = 0;
  size_t i = start_position;
  for (; i < num_dests && src_pos < num_inserts; ++i) {
    if ((*dst_suggestions)[i] != nullptr)
      continue;
    size_t src = insert_positions[src_pos++];
    std::swap((*dst_suggestions)[i], (*src_suggestions)[src]);
  }
  // Return destination positions filled so far which becomes the start_position
  // for future runs.
  return i;
}

void MostVisitedSites::NotifyMostVisitedURLsObserver() {
  size_t num_suggestions = current_suggestions_.size();
  if (received_most_visited_sites_ && received_popular_sites_ &&
      !recorded_uma_) {
    RecordImpressionUMAMetrics();
    UMA_HISTOGRAM_SPARSE_SLOWLY("NewTabPage.NumberOfTiles", num_suggestions);
    recorded_uma_ = true;
  }

  if (!observer_)
    return;

  observer_->OnMostVisitedURLsAvailable(current_suggestions_);
}

void MostVisitedSites::OnPopularSitesAvailable(bool success) {
  received_popular_sites_ = true;

  if (!success) {
    LOG(WARNING) << "Download of popular sites failed";
    return;
  }

  // Pass the popular sites to the observer. This will cause it to fetch any
  // missing icons, but will *not* cause it to display the popular sites.
  observer_->OnPopularURLsAvailable(popular_sites_->sites());

  // Re-build the suggestions list. Once done, this will notify the observer.
  BuildCurrentSuggestions();
}

void MostVisitedSites::RecordImpressionUMAMetrics() {
  for (size_t i = 0; i < current_suggestions_.size(); i++) {
    std::string histogram = base::StringPrintf(
        "NewTabPage.SuggestionsImpression.%s",
        GetSourceHistogramName(current_suggestions_[i]).c_str());
    LogHistogramEvent(histogram, static_cast<int>(i), num_sites_);
  }
}

void MostVisitedSites::TopSitesLoaded(TopSites* top_sites) {}

void MostVisitedSites::TopSitesChanged(TopSites* top_sites,
                                       ChangeReason change_reason) {
  if (mv_source_ == TOP_SITES) {
    // The displayed suggestions are invalidated.
    InitiateTopSitesQuery();
  }
}
