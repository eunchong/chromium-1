// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.android_webview.test;

import android.content.Context;
import android.test.suitebuilder.annotation.MediumTest;
import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.android_webview.AwBrowserProcess;
import org.chromium.android_webview.AwContents;
import org.chromium.android_webview.AwCookieManager;
import org.chromium.android_webview.AwWebResourceResponse;
import org.chromium.android_webview.test.util.CommonResources;
import org.chromium.android_webview.test.util.CookieUtils;
import org.chromium.base.test.util.Feature;
import org.chromium.net.test.util.TestWebServer;


/**
 * Tests for CookieManager/Chromium startup ordering weirdness.
 */
public class CookieManagerStartupTest extends AwTestBase {

    private TestAwContentsClient mContentsClient;
    private AwContents mAwContents;

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        // CookeManager assumes that native is loaded, but webview browser should not be loaded for
        // these tests as webview is not necessarily loaded when CookieManager is called.
        AwBrowserProcess.loadLibrary(
                getInstrumentation().getTargetContext().getApplicationContext());
    }

    @Override
    protected boolean needsBrowserProcessStarted() {
        return false;
    }

    private void startChromium() throws Exception {
        startChromiumWithClient(new TestAwContentsClient());
    }

    private void startChromiumWithClient(TestAwContentsClient contentsClient) throws Exception {
        final Context context = getActivity();
        getInstrumentation().runOnMainSync(new Runnable() {
            @Override
            public void run() {
                AwBrowserProcess.start(context);
            }
        });

        mContentsClient = contentsClient;
        final AwTestContainerView testContainerView =
                createAwTestContainerViewOnMainSync(mContentsClient);
        mAwContents = testContainerView.getAwContents();
        mAwContents.getSettings().setJavaScriptEnabled(true);
    }

    @MediumTest
    @Feature({"AndroidWebView"})
    public void testStartup() throws Throwable {
        TestWebServer webServer = TestWebServer.start();
        try {
            String path = "/cookie_test.html";
            String url = webServer.setResponse(path, CommonResources.ABOUT_HTML, null);

            AwCookieManager cookieManager = new AwCookieManager();
            assertNotNull(cookieManager);

            CookieUtils.clearCookies(this, cookieManager);
            assertFalse(cookieManager.hasCookies());

            cookieManager.setAcceptCookie(true);
            assertTrue(cookieManager.acceptCookie());

            cookieManager.setCookie(url, "count=41");

            startChromium();
            loadUrlSync(mAwContents, mContentsClient.getOnPageFinishedHelper(), url);
            executeJavaScriptAndWaitForResult(
                    mAwContents,
                    mContentsClient,
                    "var c=document.cookie.split('=');document.cookie=c[0]+'='+(1+(+c[1]));");

            assertEquals("count=42", cookieManager.getCookie(url));
        } finally {
            webServer.shutdown();
        }
    }

    @SmallTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testAllowFileSchemeCookies() throws Throwable {
        AwCookieManager cookieManager = new AwCookieManager();
        assertFalse(cookieManager.allowFileSchemeCookies());
        cookieManager.setAcceptFileSchemeCookies(true);
        assertTrue(cookieManager.allowFileSchemeCookies());
        cookieManager.setAcceptFileSchemeCookies(false);
        assertFalse(cookieManager.allowFileSchemeCookies());
    }

    @SmallTest
    @Feature({"AndroidWebView", "Privacy"})
    public void testAllowCookies() throws Throwable {
        AwCookieManager cookieManager = new AwCookieManager();
        assertTrue(cookieManager.acceptCookie());
        cookieManager.setAcceptCookie(false);
        assertFalse(cookieManager.acceptCookie());
        cookieManager.setAcceptCookie(true);
        assertTrue(cookieManager.acceptCookie());
    }

    // https://code.google.com/p/chromium/issues/detail?id=374203
    @MediumTest
    @Feature({"AndroidWebView"})
    public void testShouldInterceptRequestDeadlock() throws Throwable {
        String url = "http://www.example.com";
        TestAwContentsClient contentsClient = new TestAwContentsClient() {
            @Override
            public AwWebResourceResponse shouldInterceptRequest(AwWebResourceRequest request) {
                (new AwCookieManager()).getCookie("www.example.com");
                return null;
            }
        };
        startChromiumWithClient(contentsClient);
        loadUrlSync(mAwContents, contentsClient.getOnPageFinishedHelper(), url);
    }
}
