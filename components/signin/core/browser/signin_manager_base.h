// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The signin manager encapsulates some functionality tracking
// which user is signed in.
//
// **NOTE** on semantics of SigninManager:
//
// Once a signin is successful, the username becomes "established" and will not
// be cleared until a SignOut operation is performed (persists across
// restarts). Until that happens, the signin manager can still be used to
// refresh credentials, but changing the username is not permitted.
//
// On Chrome OS, because of the existence of other components that handle login
// and signin at a higher level, all that is needed from a SigninManager is
// caching / handling of the "authenticated username" field, and TokenService
// initialization, so that components that depend on these two things
// (i.e on desktop) can continue using it / don't need to change. For this
// reason, SigninManagerBase is all that exists on Chrome OS. For desktop,
// see signin/signin_manager.h.

#ifndef COMPONENTS_SIGNIN_CORE_BROWSER_SIGNIN_MANAGER_BASE_H_
#define COMPONENTS_SIGNIN_CORE_BROWSER_SIGNIN_MANAGER_BASE_H_

#include <memory>
#include <string>

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/prefs/pref_change_registrar.h"
#include "components/prefs/pref_member.h"
#include "components/signin/core/browser/account_info.h"
#include "components/signin/core/browser/signin_internals_util.h"
#include "google_apis/gaia/google_service_auth_error.h"

class AccountTrackerService;
class PrefRegistrySimple;
class PrefService;
class SigninClient;

namespace user_prefs {
class PrefRegistrySyncable;
}

class SigninManagerBase : public KeyedService {
 public:
  class Observer {
   public:
    // Called when a user fails to sign into Google services such as sync.
    virtual void GoogleSigninFailed(const GoogleServiceAuthError& error) {}

    // Called when a user signs into Google services such as sync.
    virtual void GoogleSigninSucceeded(const std::string& account_id,
                                       const std::string& username,
                                       const std::string& password) {}

    // Called when the currently signed-in user for a user has been signed out.
    virtual void GoogleSignedOut(const std::string& account_id,
                                 const std::string& username) {}

   protected:
    virtual ~Observer() {}
  };

  SigninManagerBase(SigninClient* client,
                    AccountTrackerService* account_tracker_service);
  ~SigninManagerBase() override;

  // Registers per-profile prefs.
  static void RegisterProfilePrefs(user_prefs::PrefRegistrySyncable* registry);

  // Registers per-install prefs.
  static void RegisterPrefs(PrefRegistrySimple* registry);

  // If user was signed in, load tokens from DB if available.
  virtual void Initialize(PrefService* local_state);
  bool IsInitialized() const;

  // Returns true if a signin to Chrome is allowed (by policy or pref).
  // TODO(tim): kSigninAllowed is defined for all platforms in pref_names.h.
  // If kSigninAllowed pref was non-Chrome OS-only, this method wouldn't be
  // needed, but as is we provide this method to let all interested code
  // code query the value in one way, versus half using PrefService directly
  // and the other half using SigninManager.
  virtual bool IsSigninAllowed() const;

  // If a user has previously signed in (and has not signed out), this returns
  // the know information of the account. Otherwise, it returns an empty struct.
  AccountInfo GetAuthenticatedAccountInfo() const;

  // If a user has previously signed in (and has not signed out), this returns
  // the account id. Otherwise, it returns an empty string.  This id can be used
  // to uniquely identify an account, so for example can be used as a key to
  // map accounts to data.
  //
  // TODO(rogerta): eventually the account id should be an obfuscated gaia id.
  // For now though, this function returns the same value as
  // GetAuthenticatedAccountInfo().email since lots of code assumes the unique
  // id for an account is the username.  For code that needs a unique id to
  // represent the connected account, call this method. Example: the
  // AccountStatusMap type in MutableProfileOAuth2TokenService.  For code that
  // needs to know the normalized email address of the connected account, use
  // GetAuthenticatedAccountInfo().email.  Example: to show the string "Signed
  // in as XXX" in the hotdog menu.
  const std::string& GetAuthenticatedAccountId() const;

  // Sets the authenticated user's Gaia ID and display email.  Internally,
  // this will seed the account information in AccountTrackerService and pick
  // the right account_id for this account.
  void SetAuthenticatedAccountInfo(const std::string& gaia_id,
                                   const std::string& email);

  // Returns true if there is an authenticated user.
  bool IsAuthenticated() const;

  // Returns true if there's a signin in progress.
  virtual bool AuthInProgress() const;

  // KeyedService implementation.
  void Shutdown() override;

  // Methods to register or remove observers of signin.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  // Methods to register or remove SigninDiagnosticObservers.
  void AddSigninDiagnosticsObserver(
      signin_internals_util::SigninDiagnosticsObserver* observer);
  void RemoveSigninDiagnosticsObserver(
      signin_internals_util::SigninDiagnosticsObserver* observer);

  // Gives access to the SigninClient instance associated with this instance.
  SigninClient* signin_client() const { return client_; }

 protected:
  AccountTrackerService* account_tracker_service() const {
    return account_tracker_service_;
  }

  // Sets the authenticated user's account id.
  void SetAuthenticatedAccountId(const std::string& account_id);

  // Used by subclass to clear the authenticated user instead of using
  // SetAuthenticatedAccountId, which enforces special preconditions due
  // to the fact that it is part of the public API and called by clients.
  void clear_authenticated_user() { authenticated_account_id_.clear(); }

  // List of observers to notify on signin events.
  // Makes sure list is empty on destruction.
  base::ObserverList<Observer, true> observer_list_;

  // Helper method to notify all registered diagnostics observers with.
  void NotifyDiagnosticsObservers(
      const signin_internals_util::TimedSigninStatusField& field,
      const std::string& value);

 private:
  friend class FakeSigninManagerBase;
  friend class FakeSigninManager;

  SigninClient* client_;
  AccountTrackerService* account_tracker_service_;
  bool initialized_;

  // Account id after successful authentication.
  std::string authenticated_account_id_;

  // The list of SigninDiagnosticObservers.
  base::ObserverList<signin_internals_util::SigninDiagnosticsObserver, true>
      signin_diagnostics_observers_;

  base::WeakPtrFactory<SigninManagerBase> weak_pointer_factory_;

  DISALLOW_COPY_AND_ASSIGN(SigninManagerBase);
};

#endif  // COMPONENTS_SIGNIN_CORE_BROWSER_SIGNIN_MANAGER_BASE_H_
