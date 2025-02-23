// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_NTP_SNIPPETS_NTP_SNIPPETS_SERVICE_H_
#define COMPONENTS_NTP_SNIPPETS_NTP_SNIPPETS_SERVICE_H_

#include <stddef.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "base/sequenced_task_runner.h"
#include "base/timer/timer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/ntp_snippets/ntp_snippet.h"
#include "components/ntp_snippets/ntp_snippets_fetcher.h"
#include "components/ntp_snippets/ntp_snippets_scheduler.h"
#include "components/suggestions/suggestions_service.h"

class PrefRegistrySimple;
class PrefService;
class SkBitmap;

namespace base {
class Value;
}

namespace image_fetcher {
class ImageFetcher;
}

namespace suggestions {
class SuggestionsProfile;
}

namespace ntp_snippets {

class NTPSnippetsServiceObserver;

// Stores and vends fresh content data for the NTP.
class NTPSnippetsService : public KeyedService {
 public:
  using NTPSnippetStorage = NTPSnippet::PtrVector;

  using ImageFetchedCallback =
      base::Callback<void(const std::string& snippet_id, const SkBitmap*)>;

  // |application_language_code| should be a ISO 639-1 compliant string, e.g.
  // 'en' or 'en-US'. Note that this code should only specify the language, not
  // the locale, so 'en_US' (English language with US locale) and 'en-GB_US'
  // (British English person in the US) are not language codes.
  NTPSnippetsService(
      PrefService* pref_service,
      suggestions::SuggestionsService* suggestions_service,
      scoped_refptr<base::SequencedTaskRunner> file_task_runner,
      const std::string& application_language_code,
      NTPSnippetsScheduler* scheduler,
      std::unique_ptr<NTPSnippetsFetcher> snippets_fetcher,
      std::unique_ptr<image_fetcher::ImageFetcher> image_fetcher);
  ~NTPSnippetsService() override;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  void Init(bool enabled);

  // Inherited from KeyedService.
  void Shutdown() override;

  // Fetches snippets from the server and adds them to the current ones.
  void FetchSnippets();
  // Fetches snippets from the server for specified hosts (overriding
  // suggestions from the suggestion service) and adds them to the current ones.
  void FetchSnippetsFromHosts(const std::set<std::string>& hosts);

  // Available snippets.
  const NTPSnippetStorage& snippets() const { return snippets_; }

  // Returns the list of snippets previously discarded by the user (that are
  // not expired yet).
  const NTPSnippetStorage& discarded_snippets() const {
    return discarded_snippets_;
  }

  // Returns the last status message from the snippets fetcher.
  const std::string& last_status() const {
    return snippets_fetcher_->last_status();
  }

  // Returns the last json from the snippets fetcher.
  const std::string& last_json() const {
    return snippets_fetcher_->last_json();
  }

  // (Re)schedules the periodic fetching of snippets. This is necessary because
  // the schedule depends on the time of day.
  void RescheduleFetching();

  // Fetches the image for the snippet with the given |snippet_id| and runs the
  // |callback|. If that snippet doesn't exist or the fetch fails, the callback
  // gets a null image.
  void FetchSnippetImage(const std::string& snippet_id,
                         const ImageFetchedCallback& callback);

  // Deletes all currently stored snippets.
  void ClearSnippets();

  // Discards the snippet with the given |snippet_id|, if it exists. Returns
  // true iff a snippet was discarded.
  bool DiscardSnippet(const std::string& snippet_id);

  // Clears the lists of snippets previously discarded by the user.
  void ClearDiscardedSnippets();

  // Returns the lists of suggestion hosts the snippets are restricted to.
  std::set<std::string> GetSuggestionsHosts() const;

  // Observer accessors.
  void AddObserver(NTPSnippetsServiceObserver* observer);
  void RemoveObserver(NTPSnippetsServiceObserver* observer);

  // Returns the maximum number of snippets that will be shown at once.
  static int GetMaxSnippetCountForTesting();

 private:
  void OnSuggestionsChanged(const suggestions::SuggestionsProfile& suggestions);
  void OnFetchFinished(NTPSnippetsFetcher::OptionalSnippets snippets);

  // Expects a top-level dictionary containing a "recos" list, each element of
  // which will be parsed as a snippet.
  bool LoadFromFetchedValue(const base::Value& value);

  // Merges newly available snippets with the previously available list.
  void MergeSnippets(NTPSnippetStorage new_snippets);
  // TODO(treib): Investigate a better storage, maybe LevelDB or SQLite?
  void LoadSnippetsFromPrefs();
  void StoreSnippetsToPrefs();

  void LoadDiscardedSnippetsFromPrefs();
  void StoreDiscardedSnippetsToPrefs();

  std::set<std::string> GetSnippetHostsFromPrefs() const;
  void StoreSnippetHostsToPrefs(const std::set<std::string>& hosts);

  void LoadingSnippetsFinished();

  bool enabled_;

  PrefService* pref_service_;

  suggestions::SuggestionsService* suggestions_service_;

  // The SequencedTaskRunner on which file system operations will be run.
  scoped_refptr<base::SequencedTaskRunner> file_task_runner_;

  // All current suggestions (i.e. not discarded ones).
  NTPSnippetStorage snippets_;

  // Suggestions that the user discarded. We keep these around until they expire
  // so we won't re-add them on the next fetch.
  NTPSnippetStorage discarded_snippets_;

  // The ISO 639-1 code of the language used by the application.
  const std::string application_language_code_;

  // The observers.
  base::ObserverList<NTPSnippetsServiceObserver> observers_;

  // Scheduler for fetching snippets. Not owned.
  NTPSnippetsScheduler* scheduler_;

  // The subscription to the SuggestionsService. When the suggestions change,
  // SuggestionsService will call |OnSuggestionsChanged|, which triggers an
  // update to the set of snippets.
  using SuggestionsSubscription =
      suggestions::SuggestionsService::ResponseCallbackList::Subscription;
  std::unique_ptr<SuggestionsSubscription> suggestions_service_subscription_;

  // The snippets fetcher.
  std::unique_ptr<NTPSnippetsFetcher> snippets_fetcher_;

  // Timer that calls us back when the next snippet expires.
  base::OneShotTimer expiry_timer_;

  std::unique_ptr<image_fetcher::ImageFetcher> image_fetcher_;

  DISALLOW_COPY_AND_ASSIGN(NTPSnippetsService);
};

class NTPSnippetsServiceObserver {
 public:
  // Sent every time the service loads a new set of data.
  virtual void NTPSnippetsServiceLoaded() = 0;
  // Sent when the service is shutting down.
  virtual void NTPSnippetsServiceShutdown() = 0;

 protected:
  virtual ~NTPSnippetsServiceObserver() {}
};

}  // namespace ntp_snippets

#endif  // COMPONENTS_NTP_SNIPPETS_NTP_SNIPPETS_SERVICE_H_
