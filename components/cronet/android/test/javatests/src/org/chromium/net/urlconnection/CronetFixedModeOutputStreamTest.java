// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net.urlconnection;

import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.base.test.util.Feature;
import org.chromium.net.CronetTestBase;
import org.chromium.net.CronetTestFramework;
import org.chromium.net.NativeTestServer;

import java.io.OutputStream;
import java.net.HttpRetryException;
import java.net.HttpURLConnection;
import java.net.ProtocolException;
import java.net.URL;

/**
 * Tests {@code getOutputStream} when {@code setFixedLengthStreamingMode} is
 * enabled.
 * Tests annotated with {@code CompareDefaultWithCronet} will run once with the
 * default HttpURLConnection implementation and then with Cronet's
 * HttpURLConnection implementation. Tests annotated with
 * {@code OnlyRunCronetHttpURLConnection} only run Cronet's implementation.
 * See {@link CronetTestBase#runTest()} for details.
 */
public class CronetFixedModeOutputStreamTest extends CronetTestBase {
    @Override
    protected void setUp() throws Exception {
        super.setUp();
        String[] commandLineArgs = {
                CronetTestFramework.LIBRARY_INIT_KEY,
                CronetTestFramework.LibraryInitType.HTTP_URL_CONNECTION,
        };
        startCronetTestFrameworkWithUrlAndCommandLineArgs(null, commandLineArgs);
        assertTrue(NativeTestServer.startNativeTestServer(getContext()));
    }

    @Override
    protected void tearDown() throws Exception {
        NativeTestServer.shutdownNativeTestServer();
        super.tearDown();
    }

    @SmallTest
    @Feature({"Cronet"})
    @CompareDefaultWithCronet
    public void testConnectBeforeWrite() throws Exception {
        URL url = new URL(NativeTestServer.getEchoBodyURL());
        HttpURLConnection connection =
                (HttpURLConnection) url.openConnection();
        connection.setDoOutput(true);
        connection.setRequestMethod("POST");
        connection.setFixedLengthStreamingMode(TestUtil.UPLOAD_DATA.length);
        OutputStream out = connection.getOutputStream();
        connection.connect();
        out.write(TestUtil.UPLOAD_DATA);
        assertEquals(200, connection.getResponseCode());
        assertEquals("OK", connection.getResponseMessage());
        assertEquals(TestUtil.UPLOAD_DATA_STRING, TestUtil.getResponseAsString(connection));
        connection.disconnect();
    }

    @SmallTest
    @Feature({"Cronet"})
    @CompareDefaultWithCronet
    public void testFixedLengthStreamingModeZeroContentLength()
            throws Exception {
        // Check content length is set.
        URL echoLength = new URL(NativeTestServer.getEchoHeaderURL("Content-Length"));
        HttpURLConnection connection1 =
                (HttpURLConnection) echoLength.openConnection();
        connection1.setDoOutput(true);
        connection1.setRequestMethod("POST");
        connection1.setFixedLengthStreamingMode(0);
        assertEquals(200, connection1.getResponseCode());
        assertEquals("OK", connection1.getResponseMessage());
        assertEquals("0", TestUtil.getResponseAsString(connection1));
        connection1.disconnect();

        // Check body is empty.
        URL echoBody = new URL(NativeTestServer.getEchoBodyURL());
        HttpURLConnection connection2 =
                (HttpURLConnection) echoBody.openConnection();
        connection2.setDoOutput(true);
        connection2.setRequestMethod("POST");
        connection2.setFixedLengthStreamingMode(0);
        assertEquals(200, connection2.getResponseCode());
        assertEquals("OK", connection2.getResponseMessage());
        assertEquals("", TestUtil.getResponseAsString(connection2));
        connection2.disconnect();
    }

    @SmallTest
    @Feature({"Cronet"})
    @CompareDefaultWithCronet
    public void testWriteLessThanContentLength()
            throws Exception {
        URL url = new URL(NativeTestServer.getEchoBodyURL());
        HttpURLConnection connection =
                (HttpURLConnection) url.openConnection();
        connection.setDoOutput(true);
        connection.setRequestMethod("POST");
        // Set a content length that's 1 byte more.
        connection.setFixedLengthStreamingMode(TestUtil.UPLOAD_DATA.length + 1);
        OutputStream out = connection.getOutputStream();
        out.write(TestUtil.UPLOAD_DATA);
        try {
            connection.getResponseCode();
            fail();
        } catch (ProtocolException e) {
            // Expected.
        }
        connection.disconnect();
    }

    @SmallTest
    @Feature({"Cronet"})
    @CompareDefaultWithCronet
    public void testWriteMoreThanContentLength()
            throws Exception {
        URL url = new URL(NativeTestServer.getEchoBodyURL());
        HttpURLConnection connection =
                (HttpURLConnection) url.openConnection();
        connection.setDoOutput(true);
        connection.setRequestMethod("POST");
        // Set a content length that's 1 byte short.
        connection.setFixedLengthStreamingMode(TestUtil.UPLOAD_DATA.length - 1);
        OutputStream out = connection.getOutputStream();
        try {
            out.write(TestUtil.UPLOAD_DATA);
            // On Lollipop, default implementation only triggers the error when reading response.
            connection.getInputStream();
            fail();
        } catch (ProtocolException e) {
            // Expected.
            assertEquals("expected " + (TestUtil.UPLOAD_DATA.length - 1) + " bytes but received "
                            + TestUtil.UPLOAD_DATA.length,
                    e.getMessage());
        }
        connection.disconnect();
    }

    @SmallTest
    @Feature({"Cronet"})
    @CompareDefaultWithCronet
    public void testWriteMoreThanContentLengthWriteOneByte()
            throws Exception {
        URL url = new URL(NativeTestServer.getEchoBodyURL());
        HttpURLConnection connection =
                (HttpURLConnection) url.openConnection();
        connection.setDoOutput(true);
        connection.setRequestMethod("POST");
        // Set a content length that's 1 byte short.
        connection.setFixedLengthStreamingMode(TestUtil.UPLOAD_DATA.length - 1);
        OutputStream out = connection.getOutputStream();
        for (int i = 0; i < TestUtil.UPLOAD_DATA.length - 1; i++) {
            out.write(TestUtil.UPLOAD_DATA[i]);
        }
        try {
            // Try upload an extra byte.
            out.write(TestUtil.UPLOAD_DATA[TestUtil.UPLOAD_DATA.length - 1]);
            // On Lollipop, default implementation only triggers the error when reading response.
            connection.getInputStream();
            fail();
        } catch (ProtocolException e) {
            // Expected.
            String expectedVariant = "expected 0 bytes but received 1";
            String expectedVariantOnLollipop = "expected " + (TestUtil.UPLOAD_DATA.length - 1)
                    + " bytes but received " + TestUtil.UPLOAD_DATA.length;
            assertTrue(expectedVariant.equals(e.getMessage())
                    || expectedVariantOnLollipop.equals(e.getMessage()));
        }
        connection.disconnect();
    }

    @SmallTest
    @Feature({"Cronet"})
    @CompareDefaultWithCronet
    public void testFixedLengthStreamingMode() throws Exception {
        URL url = new URL(NativeTestServer.getEchoBodyURL());
        HttpURLConnection connection =
                (HttpURLConnection) url.openConnection();
        connection.setDoOutput(true);
        connection.setRequestMethod("POST");
        connection.setFixedLengthStreamingMode(TestUtil.UPLOAD_DATA.length);
        OutputStream out = connection.getOutputStream();
        out.write(TestUtil.UPLOAD_DATA);
        assertEquals(200, connection.getResponseCode());
        assertEquals("OK", connection.getResponseMessage());
        assertEquals(TestUtil.UPLOAD_DATA_STRING, TestUtil.getResponseAsString(connection));
        connection.disconnect();
    }

    @SmallTest
    @Feature({"Cronet"})
    @CompareDefaultWithCronet
    public void testFixedLengthStreamingModeWriteOneByte()
            throws Exception {
        URL url = new URL(NativeTestServer.getEchoBodyURL());
        HttpURLConnection connection =
                (HttpURLConnection) url.openConnection();
        connection.setDoOutput(true);
        connection.setRequestMethod("POST");
        connection.setFixedLengthStreamingMode(TestUtil.UPLOAD_DATA.length);
        OutputStream out = connection.getOutputStream();
        for (int i = 0; i < TestUtil.UPLOAD_DATA.length; i++) {
            // Write one byte at a time.
            out.write(TestUtil.UPLOAD_DATA[i]);
        }
        assertEquals(200, connection.getResponseCode());
        assertEquals("OK", connection.getResponseMessage());
        assertEquals(TestUtil.UPLOAD_DATA_STRING, TestUtil.getResponseAsString(connection));
        connection.disconnect();
    }

    @SmallTest
    @Feature({"Cronet"})
    @CompareDefaultWithCronet
    public void testFixedLengthStreamingModeLargeData() throws Exception {
        URL url = new URL(NativeTestServer.getEchoBodyURL());
        HttpURLConnection connection =
                (HttpURLConnection) url.openConnection();
        connection.setDoOutput(true);
        connection.setRequestMethod("POST");
        // largeData is 1.8 MB.
        byte[] largeData = TestUtil.getLargeData();
        connection.setFixedLengthStreamingMode(largeData.length);
        OutputStream out = connection.getOutputStream();
        int totalBytesWritten = 0;
        // Number of bytes to write each time. It is doubled each time
        // to make sure that the implementation can handle large writes.
        int bytesToWrite = 683;
        while (totalBytesWritten < largeData.length) {
            if (bytesToWrite > largeData.length - totalBytesWritten) {
                // Do not write out of bound.
                bytesToWrite = largeData.length - totalBytesWritten;
            }
            out.write(largeData, totalBytesWritten, bytesToWrite);
            totalBytesWritten += bytesToWrite;
            // About 5th iteration of this loop, bytesToWrite will be bigger than 16384.
            bytesToWrite *= 2;
        }
        assertEquals(200, connection.getResponseCode());
        assertEquals("OK", connection.getResponseMessage());
        TestUtil.checkLargeData(TestUtil.getResponseAsString(connection));
        connection.disconnect();
    }

    @SmallTest
    @Feature({"Cronet"})
    @CompareDefaultWithCronet
    public void testFixedLengthStreamingModeLargeDataWriteOneByte()
            throws Exception {
        URL url = new URL(NativeTestServer.getEchoBodyURL());
        HttpURLConnection connection =
                (HttpURLConnection) url.openConnection();
        connection.setDoOutput(true);
        connection.setRequestMethod("POST");
        byte[] largeData = TestUtil.getLargeData();
        connection.setFixedLengthStreamingMode(largeData.length);
        OutputStream out = connection.getOutputStream();
        for (int i = 0; i < largeData.length; i++) {
            // Write one byte at a time.
            out.write(largeData[i]);
        }
        assertEquals(200, connection.getResponseCode());
        assertEquals("OK", connection.getResponseMessage());
        TestUtil.checkLargeData(TestUtil.getResponseAsString(connection));
        connection.disconnect();
    }

    @SmallTest
    @Feature({"Cronet"})
    @OnlyRunCronetHttpURLConnection
    public void testLargeDataMoreThanNativeBufferSize()
            throws Exception {
        // Set an internal buffer of size larger than the buffer size used
        // in network stack internally.
        // Normal stream uses 16384, QUIC uses 14520, and SPDY uses 16384.
        CronetFixedModeOutputStream.setDefaultBufferLengthForTesting(17384);
        testFixedLengthStreamingModeLargeDataWriteOneByte();
        testFixedLengthStreamingModeLargeData();
    }

    @SmallTest
    @Feature({"Cronet"})
    @CompareDefaultWithCronet
    public void testOneMassiveWrite() throws Exception {
        URL url = new URL(NativeTestServer.getEchoBodyURL());
        HttpURLConnection connection =
                (HttpURLConnection) url.openConnection();
        connection.setDoOutput(true);
        connection.setRequestMethod("POST");
        byte[] largeData = TestUtil.getLargeData();
        connection.setFixedLengthStreamingMode(largeData.length);
        OutputStream out = connection.getOutputStream();
        // Write everything at one go, so the data is larger than the buffer
        // used in CronetFixedModeOutputStream.
        out.write(largeData);
        assertEquals(200, connection.getResponseCode());
        assertEquals("OK", connection.getResponseMessage());
        TestUtil.checkLargeData(TestUtil.getResponseAsString(connection));
        connection.disconnect();
    }

    @SmallTest
    @Feature({"Cronet"})
    @CompareDefaultWithCronet
    public void testRewind() throws Exception {
        // Post preserving redirect should fail.
        URL url = new URL(NativeTestServer.getRedirectToEchoBody());
        HttpURLConnection connection =
                (HttpURLConnection) url.openConnection();
        connection.setDoOutput(true);
        connection.setRequestMethod("POST");
        connection.setFixedLengthStreamingMode(TestUtil.UPLOAD_DATA.length);
        try {
            OutputStream out = connection.getOutputStream();
            out.write(TestUtil.UPLOAD_DATA);
        } catch (HttpRetryException e) {
            assertEquals("Cannot retry streamed Http body", e.getMessage());
        }
        connection.disconnect();
    }
}
