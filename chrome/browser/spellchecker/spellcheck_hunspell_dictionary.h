// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SPELLCHECKER_SPELLCHECK_HUNSPELL_DICTIONARY_H_
#define CHROME_BROWSER_SPELLCHECKER_SPELLCHECK_HUNSPELL_DICTIONARY_H_

#include <memory>
#include <string>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/move.h"
#include "base/observer_list.h"
#include "chrome/browser/spellchecker/spellcheck_dictionary.h"
#include "net/url_request/url_fetcher_delegate.h"

class GURL;
class SpellcheckService;

namespace net {
class URLFetcher;
class URLRequestContextGetter;
}  // namespace net

// Defines the browser-side hunspell dictionary and provides access to it.
class SpellcheckHunspellDictionary
    : public SpellcheckDictionary,
      public net::URLFetcherDelegate,
      public base::SupportsWeakPtr<SpellcheckHunspellDictionary> {
 public:
  // Interface to implement for observers of the Hunspell dictionary.
  class Observer {
   public:
    // The dictionary has been initialized.
    virtual void OnHunspellDictionaryInitialized(
        const std::string& language) = 0;

    // Dictionary download began.
    virtual void OnHunspellDictionaryDownloadBegin(
        const std::string& language) = 0;

    // Dictionary download succeeded.
    virtual void OnHunspellDictionaryDownloadSuccess(
        const std::string& language) = 0;

    // Dictionary download failed.
    virtual void OnHunspellDictionaryDownloadFailure(
        const std::string& language) = 0;
  };

  SpellcheckHunspellDictionary(
      const std::string& language,
      net::URLRequestContextGetter* request_context_getter,
      SpellcheckService* spellcheck_service);
  ~SpellcheckHunspellDictionary() override;

  // SpellcheckDictionary implementation:
  void Load() override;

  // Retry downloading |dictionary_file_|.
  void RetryDownloadDictionary(
      net::URLRequestContextGetter* request_context_getter);

  // Returns true if the dictionary is ready to use.
  virtual bool IsReady() const;

  const base::File& GetDictionaryFile() const;
  const std::string& GetLanguage() const;
  bool IsUsingPlatformChecker() const;

  // Add an observer for Hunspell dictionary events.
  void AddObserver(Observer* observer);

  // Remove an observer for Hunspell dictionary events.
  void RemoveObserver(Observer* observer);

  // Whether dictionary is being downloaded.
  bool IsDownloadInProgress();

  // Whether dictionary download failed.
  bool IsDownloadFailure();

 private:
  // Dictionary download status.
  enum DownloadStatus {
    DOWNLOAD_NONE,
    DOWNLOAD_IN_PROGRESS,
    DOWNLOAD_FAILED,
  };

  // Dictionary file information to be passed between the FILE and UI threads.
  struct DictionaryFile {
    MOVE_ONLY_TYPE_FOR_CPP_03(DictionaryFile)
   public:
    DictionaryFile();
    ~DictionaryFile();

    DictionaryFile(DictionaryFile&& other);
    DictionaryFile& operator=(DictionaryFile&& other);

    // The desired location of the dictionary file, whether or not it exists.
    base::FilePath path;

    // The dictionary file.
    base::File file;
  };

  // net::URLFetcherDelegate implementation. Called when dictionary download
  // finishes.
  void OnURLFetchComplete(const net::URLFetcher* source) override;

  // Determine the correct url to download the dictionary.
  GURL GetDictionaryURL();

  // Attempt to download the dictionary.
  void DownloadDictionary(GURL url);

  // Figures out the location for the dictionary, verifies its contents, and
  // opens it.
  static DictionaryFile OpenDictionaryFile(const base::FilePath& path);

  // Gets the default location for the dictionary file.
  static DictionaryFile InitializeDictionaryLocation(
      const std::string& language);

  // The reply point for PostTaskAndReplyWithResult, called after the dictionary
  // file has been initialized.
  void InitializeDictionaryLocationComplete(DictionaryFile file);

  // The reply point for PostTaskAndReplyWithResult, called after the dictionary
  // file has been saved.
  void SaveDictionaryDataComplete(bool dictionary_saved);

  // Notify listeners that the dictionary has been initialized.
  void InformListenersOfInitialization();

  // Notify listeners that the dictionary download failed.
  void InformListenersOfDownloadFailure();

  // The language of the dictionary file.
  std::string language_;

  // Whether to use the platform spellchecker instead of Hunspell.
  bool use_browser_spellchecker_;

  // Used for downloading the dictionary file. SpellcheckHunspellDictionary does
  // not hold a reference, and it is only valid to use it on the UI thread.
  net::URLRequestContextGetter* request_context_getter_;

  // Used for downloading the dictionary file.
  std::unique_ptr<net::URLFetcher> fetcher_;

  SpellcheckService* spellcheck_service_;

  // Observers of Hunspell dictionary events.
  base::ObserverList<Observer> observers_;

  // Status of the dictionary download.
  DownloadStatus download_status_;

  // Dictionary file path and descriptor.
  DictionaryFile dictionary_file_;

  base::WeakPtrFactory<SpellcheckHunspellDictionary> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(SpellcheckHunspellDictionary);
};

#endif  // CHROME_BROWSER_SPELLCHECKER_SPELLCHECK_HUNSPELL_DICTIONARY_H_
