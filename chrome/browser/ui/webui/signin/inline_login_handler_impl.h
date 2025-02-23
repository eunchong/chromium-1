// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_SIGNIN_INLINE_LOGIN_HANDLER_IMPL_H_
#define CHROME_BROWSER_UI_WEBUI_SIGNIN_INLINE_LOGIN_HANDLER_IMPL_H_

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/sync/one_click_signin_sync_starter.h"
#include "chrome/browser/ui/webui/signin/inline_login_handler.h"
#include "google_apis/gaia/gaia_auth_consumer.h"

// Implementation for the inline login WebUI handler on desktop Chrome. Once
// CrOS migrates to the same webview approach as desktop Chrome, much of the
// code in this class should move to its base class |InlineLoginHandler|.
class InlineLoginHandlerImpl : public InlineLoginHandler,
                               public content::WebContentsObserver {
 public:
  InlineLoginHandlerImpl();
  ~InlineLoginHandlerImpl() override;

  using InlineLoginHandler::web_ui;

  base::WeakPtr<InlineLoginHandlerImpl> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  Browser* GetDesktopBrowser();
  void SyncStarterCallback(OneClickSigninSyncStarter::SyncSetupResult result);
  // Closes the current tab and shows the account management view of the avatar
  // bubble if |show_account_management| is true.
  void CloseTab(bool show_account_management);
  void HandleLoginError(const std::string& error_msg);

 private:
  friend class InlineLoginUIBrowserTest;
  FRIEND_TEST_ALL_PREFIXES(InlineLoginUIBrowserTest, CanOfferNoProfile);
  FRIEND_TEST_ALL_PREFIXES(InlineLoginUIBrowserTest, CanOffer);
  FRIEND_TEST_ALL_PREFIXES(InlineLoginUIBrowserTest, CanOfferProfileConnected);
  FRIEND_TEST_ALL_PREFIXES(InlineLoginUIBrowserTest,
                           CanOfferUsernameNotAllowed);
  FRIEND_TEST_ALL_PREFIXES(InlineLoginUIBrowserTest, CanOfferWithRejectedEmail);
  FRIEND_TEST_ALL_PREFIXES(InlineLoginUIBrowserTest, CanOfferNoSigninCookies);

  // Argument to CanOffer().
  enum CanOfferFor {
    CAN_OFFER_FOR_ALL,
    CAN_OFFER_FOR_SECONDARY_ACCOUNT
  };

  static bool CanOffer(Profile* profile,
                       CanOfferFor can_offer_for,
                       const std::string& gaia_id,
                       const std::string& email,
                       std::string* error_message);

  // InlineLoginHandler overrides:
  void SetExtraInitParams(base::DictionaryValue& params) override;
  void CompleteLogin(const base::ListValue* args) override;

  // This struct exists to pass paramters to the FinishCompleteLogin() method,
  // since the base::Bind() call does not support this many template args.
  struct FinishCompleteLoginParams {
   public:
    FinishCompleteLoginParams(InlineLoginHandlerImpl* handler,
                              content::StoragePartition* partition,
                              const GURL& url,
                              const base::FilePath& profile_path,
                              bool confirm_untrusted_signin,
                              const std::string& email,
                              const std::string& gaia_id,
                              const std::string& password,
                              const std::string& session_index,
                              const std::string& auth_code,
                              bool choose_what_to_sync);
    FinishCompleteLoginParams(const FinishCompleteLoginParams& other);
    ~FinishCompleteLoginParams();

    // Pointer to WebUI handler.  May be nullptr.
    InlineLoginHandlerImpl* handler;
    // The isolate storage partition containing sign in cookies.
    content::StoragePartition* partition;
    // URL of sign in containing parameters such as email, source, etc.
    GURL url;
    // Path to profile being signed in.  Non empty only when signing
    // in to the profile from the user manager.
    base::FilePath profile_path;
    // When true, an extra prompt will be shown to the user before sign in
    // completes.
    bool confirm_untrusted_signin;
    // Email address of the account used to sign in.
    std::string email;
    // Obfustcated gaia id of the account used to sign in.
    std::string gaia_id;
    // Password of the account used to sign in.
    std::string password;
    // Index within gaia cookie of the account used to sign in.  Used only
    // with password combined signin flow.
    std::string session_index;
    // Authentication code used to exchange for a login scoped refresh token
    // for the account used to sign in.  Used only with password separated
    // signin flow.
    std::string auth_code;
    // True if the user wants to configure sync before signing in.
    bool choose_what_to_sync;
  };

  static void FinishCompleteLogin(const FinishCompleteLoginParams& params,
                                  Profile* profile,
                                  Profile::CreateStatus status);

  // Overridden from content::WebContentsObserver overrides.
  void DidCommitProvisionalLoadForFrame(
      content::RenderFrameHost* render_frame_host,
      const GURL& url,
      ui::PageTransition transition_type) override;

  // True if the user has navigated to untrusted domains during the signin
  // process.
  bool confirm_untrusted_signin_;

  base::WeakPtrFactory<InlineLoginHandlerImpl> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(InlineLoginHandlerImpl);
};

// Handles details of signing the user in with SigninManager and turning on
// sync after InlineLoginHandlerImpl has acquired the auth tokens from GAIA.
// This is a separate class from InlineLoginHandlerImpl because the full signin
// process is asynchronous and can outlive the signin UI.
// InlineLoginHandlerImpl is destryed once the UI is closed.
class InlineSigninHelper : public GaiaAuthConsumer {
 public:
  // Actions that can be taken when the user is asked to confirm their account.
  enum Action {
    // The user chose not to sign in to the current profile and wants chrome
    // to create a new profile instead.
    CREATE_NEW_USER,

    // The user chose to sign in and enable sync in the current profile.
    START_SYNC,

    // The user chose abort sign in.
    CLOSE
  };

  InlineSigninHelper(
      base::WeakPtr<InlineLoginHandlerImpl> handler,
      net::URLRequestContextGetter* getter,
      Profile* profile,
      const GURL& current_url,
      const std::string& email,
      const std::string& gaia_id,
      const std::string& password,
      const std::string& session_index,
      const std::string& auth_code,
      const std::string& signin_scoped_device_id,
      bool choose_what_to_sync,
      bool confirm_untrusted_signin);
  ~InlineSigninHelper() override;

 private:
  // Handles cross account sign in error. If the supplied |email| does not match
  // the last signed in email of the current profile, then Chrome will show a
  // confirmation dialog before starting sync. It returns true if there is a
  // cross account error, and false otherwise.
  bool HandleCrossAccountError(
      const std::string& refresh_token,
      OneClickSigninSyncStarter::ConfirmationRequired confirmation_required,
      OneClickSigninSyncStarter::StartSyncMode start_mode);

  // Callback used with ConfirmEmailDialogDelegate.
  void ConfirmEmailAction(
      content::WebContents* web_contents,
      const std::string& refresh_token,
      OneClickSigninSyncStarter::ConfirmationRequired confirmation_required,
      OneClickSigninSyncStarter::StartSyncMode start_mode,
      Action action);

  // Overridden from GaiaAuthConsumer.
  void OnClientOAuthSuccess(const ClientOAuthResult& result) override;
  void OnClientOAuthFailure(const GoogleServiceAuthError& error)
      override;

  // Creates the sync starter.  Virtual for tests. Call to exchange oauth code
  // for tokens.
  virtual void CreateSyncStarter(
      Browser* browser,
      content::WebContents* contents,
      const GURL& current_url,
      const GURL& continue_url,
      const std::string& refresh_token,
      OneClickSigninSyncStarter::StartSyncMode start_mode,
      OneClickSigninSyncStarter::ConfirmationRequired confirmation_required);

  GaiaAuthFetcher gaia_auth_fetcher_;
  base::WeakPtr<InlineLoginHandlerImpl> handler_;
  Profile* profile_;
  GURL current_url_;
  std::string email_;
  std::string gaia_id_;
  std::string password_;
  std::string session_index_;
  std::string auth_code_;
  bool choose_what_to_sync_;
  bool confirm_untrusted_signin_;

  DISALLOW_COPY_AND_ASSIGN(InlineSigninHelper);
};

#endif  // CHROME_BROWSER_UI_WEBUI_SIGNIN_INLINE_LOGIN_HANDLER_IMPL_H_
