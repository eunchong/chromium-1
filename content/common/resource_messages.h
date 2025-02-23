// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// IPC messages for resource loading.
//
// NOTE: All messages must send an |int request_id| as their first parameter.

// Multiply-included message file, hence no include guard.

#include <stdint.h>

#include "base/memory/shared_memory.h"
#include "base/process/process.h"
#include "content/common/content_param_traits_macros.h"
#include "content/common/navigation_params.h"
#include "content/common/resource_request_body.h"
#include "content/common/service_worker/service_worker_types.h"
#include "content/public/common/common_param_traits.h"
#include "content/public/common/resource_response.h"
#include "ipc/ipc_message_macros.h"
#include "net/base/request_priority.h"
#include "net/http/http_response_info.h"
#include "net/nqe/network_quality_estimator.h"
#include "net/url_request/redirect_info.h"

#ifndef CONTENT_COMMON_RESOURCE_MESSAGES_H_
#define CONTENT_COMMON_RESOURCE_MESSAGES_H_

namespace net {
struct LoadTimingInfo;
}

namespace content {
struct ResourceDevToolsInfo;
}

namespace IPC {

template <>
struct ParamTraits<scoped_refptr<net::HttpResponseHeaders> > {
  typedef scoped_refptr<net::HttpResponseHeaders> param_type;
  static void GetSize(base::PickleSizer* s, const param_type& p);
  static void Write(base::Pickle* m, const param_type& p);
  static bool Read(const base::Pickle* m,
                   base::PickleIterator* iter,
                   param_type* r);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct CONTENT_EXPORT ParamTraits<storage::DataElement> {
  typedef storage::DataElement param_type;
  static void GetSize(base::PickleSizer* s, const param_type& p);
  static void Write(base::Pickle* m, const param_type& p);
  static bool Read(const base::Pickle* m,
                   base::PickleIterator* iter,
                   param_type* r);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<scoped_refptr<content::ResourceDevToolsInfo> > {
  typedef scoped_refptr<content::ResourceDevToolsInfo> param_type;
  static void GetSize(base::PickleSizer* s, const param_type& p);
  static void Write(base::Pickle* m, const param_type& p);
  static bool Read(const base::Pickle* m,
                   base::PickleIterator* iter,
                   param_type* r);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<net::LoadTimingInfo> {
  typedef net::LoadTimingInfo param_type;
  static void GetSize(base::PickleSizer* s, const param_type& p);
  static void Write(base::Pickle* m, const param_type& p);
  static bool Read(const base::Pickle* m,
                   base::PickleIterator* iter,
                   param_type* r);
  static void Log(const param_type& p, std::string* l);
};

template <>
struct ParamTraits<scoped_refptr<content::ResourceRequestBody> > {
  typedef scoped_refptr<content::ResourceRequestBody> param_type;
  static void GetSize(base::PickleSizer* s, const param_type& p);
  static void Write(base::Pickle* m, const param_type& p);
  static bool Read(const base::Pickle* m,
                   base::PickleIterator* iter,
                   param_type* r);
  static void Log(const param_type& p, std::string* l);
};

}  // namespace IPC

#endif  // CONTENT_COMMON_RESOURCE_MESSAGES_H_


#define IPC_MESSAGE_START ResourceMsgStart
#undef IPC_MESSAGE_EXPORT
#define IPC_MESSAGE_EXPORT CONTENT_EXPORT

IPC_ENUM_TRAITS_MAX_VALUE( \
    net::HttpResponseInfo::ConnectionInfo, \
    net::HttpResponseInfo::NUM_OF_CONNECTION_INFOS - 1)

IPC_ENUM_TRAITS_MAX_VALUE(content::FetchRequestMode,
                          content::FETCH_REQUEST_MODE_LAST)

IPC_ENUM_TRAITS_MAX_VALUE(content::FetchCredentialsMode,
                          content::FETCH_CREDENTIALS_MODE_LAST)

IPC_ENUM_TRAITS_MAX_VALUE(content::FetchRedirectMode,
                          content::FetchRedirectMode::LAST)

IPC_ENUM_TRAITS_MAX_VALUE(
    net::NetworkQualityEstimator::EffectiveConnectionType,
    net::NetworkQualityEstimator::EFFECTIVE_CONNECTION_TYPE_LAST - 1)

IPC_STRUCT_TRAITS_BEGIN(content::ResourceResponseHead)
IPC_STRUCT_TRAITS_PARENT(content::ResourceResponseInfo)
  IPC_STRUCT_TRAITS_MEMBER(request_start)
  IPC_STRUCT_TRAITS_MEMBER(response_start)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(content::SyncLoadResult)
  IPC_STRUCT_TRAITS_PARENT(content::ResourceResponseHead)
  IPC_STRUCT_TRAITS_MEMBER(error_code)
  IPC_STRUCT_TRAITS_MEMBER(final_url)
  IPC_STRUCT_TRAITS_MEMBER(data)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(content::ResourceResponseInfo)
  IPC_STRUCT_TRAITS_MEMBER(request_time)
  IPC_STRUCT_TRAITS_MEMBER(response_time)
  IPC_STRUCT_TRAITS_MEMBER(headers)
  IPC_STRUCT_TRAITS_MEMBER(mime_type)
  IPC_STRUCT_TRAITS_MEMBER(charset)
  IPC_STRUCT_TRAITS_MEMBER(security_info)
  IPC_STRUCT_TRAITS_MEMBER(has_major_certificate_errors)
  IPC_STRUCT_TRAITS_MEMBER(content_length)
  IPC_STRUCT_TRAITS_MEMBER(encoded_data_length)
  IPC_STRUCT_TRAITS_MEMBER(appcache_id)
  IPC_STRUCT_TRAITS_MEMBER(appcache_manifest_url)
  IPC_STRUCT_TRAITS_MEMBER(load_timing)
  IPC_STRUCT_TRAITS_MEMBER(devtools_info)
  IPC_STRUCT_TRAITS_MEMBER(download_file_path)
  IPC_STRUCT_TRAITS_MEMBER(was_fetched_via_spdy)
  IPC_STRUCT_TRAITS_MEMBER(was_npn_negotiated)
  IPC_STRUCT_TRAITS_MEMBER(was_alternate_protocol_available)
  IPC_STRUCT_TRAITS_MEMBER(connection_info)
  IPC_STRUCT_TRAITS_MEMBER(was_fetched_via_proxy)
  IPC_STRUCT_TRAITS_MEMBER(npn_negotiated_protocol)
  IPC_STRUCT_TRAITS_MEMBER(socket_address)
  IPC_STRUCT_TRAITS_MEMBER(was_fetched_via_service_worker)
  IPC_STRUCT_TRAITS_MEMBER(was_fallback_required_by_service_worker)
  IPC_STRUCT_TRAITS_MEMBER(original_url_via_service_worker)
  IPC_STRUCT_TRAITS_MEMBER(response_type_via_service_worker)
  IPC_STRUCT_TRAITS_MEMBER(service_worker_start_time)
  IPC_STRUCT_TRAITS_MEMBER(service_worker_ready_time)
  IPC_STRUCT_TRAITS_MEMBER(is_in_cache_storage)
  IPC_STRUCT_TRAITS_MEMBER(cache_storage_cache_name)
  IPC_STRUCT_TRAITS_MEMBER(proxy_server)
  IPC_STRUCT_TRAITS_MEMBER(is_using_lofi)
  IPC_STRUCT_TRAITS_MEMBER(effective_connection_type)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(net::RedirectInfo)
  IPC_STRUCT_TRAITS_MEMBER(status_code)
  IPC_STRUCT_TRAITS_MEMBER(new_method)
  IPC_STRUCT_TRAITS_MEMBER(new_url)
  IPC_STRUCT_TRAITS_MEMBER(new_first_party_for_cookies)
  IPC_STRUCT_TRAITS_MEMBER(new_referrer)
  IPC_STRUCT_TRAITS_MEMBER(referred_token_binding_host)
IPC_STRUCT_TRAITS_END()

// Parameters for a resource request.
IPC_STRUCT_BEGIN(ResourceHostMsg_Request)
  // The request method: GET, POST, etc.
  IPC_STRUCT_MEMBER(std::string, method)

  // The requested URL.
  IPC_STRUCT_MEMBER(GURL, url)

  // Usually the URL of the document in the top-level window, which may be
  // checked by the third-party cookie blocking policy. Leaving it empty may
  // lead to undesired cookie blocking. Third-party cookie blocking can be
  // bypassed by setting first_party_for_cookies = url, but this should ideally
  // only be done if there really is no way to determine the correct value.
  IPC_STRUCT_MEMBER(GURL, first_party_for_cookies)

  // The origin of the context which initiated the request, which will be used
  // for cookie checks like 'First-Party-Only'.
  IPC_STRUCT_MEMBER(url::Origin, request_initiator)

  // The referrer to use (may be empty).
  IPC_STRUCT_MEMBER(GURL, referrer)

  // The referrer policy to use.
  IPC_STRUCT_MEMBER(blink::WebReferrerPolicy, referrer_policy)

  // The frame's visiblity state.
  IPC_STRUCT_MEMBER(blink::WebPageVisibilityState, visiblity_state)

  // Additional HTTP request headers.
  IPC_STRUCT_MEMBER(std::string, headers)

  // net::URLRequest load flags (0 by default).
  IPC_STRUCT_MEMBER(int, load_flags)

  // Process ID from which this request originated, or zero if it originated
  // in the renderer itself.
  // If kDirectNPAPIRequests isn't specified, then plugin requests get routed
  // through the renderer and and this holds the pid of the plugin process.
  // Otherwise this holds the render_process_id of the view that has the plugin.
  IPC_STRUCT_MEMBER(int, origin_pid)

  // What this resource load is for (main frame, sub-frame, sub-resource,
  // object).
  IPC_STRUCT_MEMBER(content::ResourceType, resource_type)

  // The priority of this request.
  IPC_STRUCT_MEMBER(net::RequestPriority, priority)

  // Used by plugin->browser requests to get the correct net::URLRequestContext.
  IPC_STRUCT_MEMBER(uint32_t, request_context)

  // Indicates which frame (or worker context) the request is being loaded into,
  // or kAppCacheNoHostId.
  IPC_STRUCT_MEMBER(int, appcache_host_id)

  // True if corresponding AppCache group should be resetted.
  IPC_STRUCT_MEMBER(bool, should_reset_appcache)

  // Indicates which frame (or worker context) the request is being loaded into,
  // or kInvalidServiceWorkerProviderId.
  IPC_STRUCT_MEMBER(int, service_worker_provider_id)

  // True if the request originated from a Service Worker, e.g. due to a
  // fetch() in the Service Worker script.
  IPC_STRUCT_MEMBER(bool, originated_from_service_worker)

  // True if the request should not be handled by the ServiceWorker.
  IPC_STRUCT_MEMBER(bool, skip_service_worker)

  // The request mode passed to the ServiceWorker.
  IPC_STRUCT_MEMBER(content::FetchRequestMode, fetch_request_mode)

  // The credentials mode passed to the ServiceWorker.
  IPC_STRUCT_MEMBER(content::FetchCredentialsMode, fetch_credentials_mode)

  // The redirect mode used in Fetch API.
  IPC_STRUCT_MEMBER(content::FetchRedirectMode, fetch_redirect_mode)

  // The request context passed to the ServiceWorker.
  IPC_STRUCT_MEMBER(content::RequestContextType, fetch_request_context_type)

  // The frame type passed to the ServiceWorker.
  IPC_STRUCT_MEMBER(content::RequestContextFrameType, fetch_frame_type)

  // Optional resource request body (may be null).
  IPC_STRUCT_MEMBER(scoped_refptr<content::ResourceRequestBody>,
                    request_body)

  IPC_STRUCT_MEMBER(bool, download_to_file)

  // True if the request was user initiated.
  IPC_STRUCT_MEMBER(bool, has_user_gesture)

  // True if load timing data should be collected for request.
  IPC_STRUCT_MEMBER(bool, enable_load_timing)

  // True if upload progress should be available for request.
  IPC_STRUCT_MEMBER(bool, enable_upload_progress)

  // True if login prompts for this request should be supressed.
  IPC_STRUCT_MEMBER(bool, do_not_prompt_for_login)

  // The routing id of the RenderFrame.
  IPC_STRUCT_MEMBER(int, render_frame_id)

  // True if |frame_id| is the main frame of a RenderView.
  IPC_STRUCT_MEMBER(bool, is_main_frame)

  // True if |parent_render_frame_id| is the main frame of a RenderView.
  IPC_STRUCT_MEMBER(bool, parent_is_main_frame)

  // Identifies the parent frame of the frame that sent the request.
  // -1 if unknown / invalid.
  IPC_STRUCT_MEMBER(int, parent_render_frame_id)

  IPC_STRUCT_MEMBER(ui::PageTransition, transition_type)

  // For navigations, whether this navigation should replace the current session
  // history entry on commit.
  IPC_STRUCT_MEMBER(bool, should_replace_current_entry)

  // The following two members identify a previous request that has been
  // created before this navigation has been transferred to a new render view.
  // This serves the purpose of recycling the old request.
  // Unless this refers to a transferred navigation, these values are -1 and -1.
  IPC_STRUCT_MEMBER(int, transferred_request_child_id)
  IPC_STRUCT_MEMBER(int, transferred_request_request_id)

  // Whether or not we should allow the URL to download.
  IPC_STRUCT_MEMBER(bool, allow_download)

  // Whether to intercept headers to pass back to the renderer.
  IPC_STRUCT_MEMBER(bool, report_raw_headers)

  // Whether or not to request a LoFi version of the resource or let the browser
  // decide.
  IPC_STRUCT_MEMBER(content::LoFiState, lofi_state)

  // PlzNavigate: the stream url associated with a navigation. Used to get
  // access to the body of the response that has already been fetched by the
  // browser.
  IPC_STRUCT_MEMBER(GURL, resource_body_stream_url)
IPC_STRUCT_END()

// Parameters for a ResourceMsg_RequestComplete
IPC_STRUCT_BEGIN(ResourceMsg_RequestCompleteData)
  // The error code.
  IPC_STRUCT_MEMBER(int, error_code)

  // Was ignored by the request handler.
  IPC_STRUCT_MEMBER(bool, was_ignored_by_handler)

  // A copy of the data requested exists in the cache.
  IPC_STRUCT_MEMBER(bool, exists_in_cache)

  // Serialized security info; see content/common/ssl_status_serialization.h.
  IPC_STRUCT_MEMBER(std::string, security_info)

  // Time the request completed.
  IPC_STRUCT_MEMBER(base::TimeTicks, completion_time)

  // Total amount of data received from the network.
  IPC_STRUCT_MEMBER(int64_t, encoded_data_length)
IPC_STRUCT_END()

// Resource messages sent from the browser to the renderer.

// Sent when the headers are available for a resource request.
IPC_MESSAGE_CONTROL2(ResourceMsg_ReceivedResponse,
                     int /* request_id */,
                     content::ResourceResponseHead)

// Sent when cached metadata from a resource request is ready.
IPC_MESSAGE_CONTROL2(ResourceMsg_ReceivedCachedMetadata,
                     int /* request_id */,
                     std::vector<char> /* data */)

// Sent as upload progress is being made.
IPC_MESSAGE_CONTROL3(ResourceMsg_UploadProgress,
                     int /* request_id */,
                     int64_t /* position */,
                     int64_t /* size */)

// Sent when the request has been redirected.  The receiver is expected to
// respond with either a FollowRedirect message (if the redirect is to be
// followed) or a CancelRequest message (if it should not be followed).
IPC_MESSAGE_CONTROL3(ResourceMsg_ReceivedRedirect,
                     int /* request_id */,
                     net::RedirectInfo /* redirect_info */,
                     content::ResourceResponseHead)

// Sent to set the shared memory buffer to be used to transmit response data to
// the renderer.  Subsequent DataReceived messages refer to byte ranges in the
// shared memory buffer.  The shared memory buffer should be retained by the
// renderer until the resource request completes.
//
// NOTE: The shared memory handle should already be mapped into the process
// that receives this message.
//
// TODO(darin): The |renderer_pid| parameter is just a temporary parameter,
// added to help in debugging crbug/160401.
//
IPC_MESSAGE_CONTROL4(ResourceMsg_SetDataBuffer,
                     int /* request_id */,
                     base::SharedMemoryHandle /* shm_handle */,
                     int /* shm_size */,
                     base::ProcessId /* renderer_pid */)

// Sent when a chunk of data from a resource request is ready, and the resource
// is expected to be small enough to fit in the inlined buffer.
// The data is sent as a part of IPC message.
IPC_MESSAGE_CONTROL3(ResourceMsg_InlinedDataChunkReceived,
                     int /* request_id */,
                     std::vector<char> /* data */,
                     int /* encoded_data_length */)

// Sent when some data from a resource request is ready.  The data offset and
// length specify a byte range into the shared memory buffer provided by the
// SetDataBuffer message.
IPC_MESSAGE_CONTROL4(ResourceMsg_DataReceived,
                     int /* request_id */,
                     int /* data_offset */,
                     int /* data_length */,
                     int /* encoded_data_length */)

// Sent when some data from a resource request has been downloaded to
// file. This is only called in the 'download_to_file' case and replaces
// ResourceMsg_DataReceived in the call sequence in that case.
IPC_MESSAGE_CONTROL3(ResourceMsg_DataDownloaded,
                     int /* request_id */,
                     int /* data_len */,
                     int /* encoded_data_length */)

// Sent when the request has been completed.
IPC_MESSAGE_CONTROL2(ResourceMsg_RequestComplete,
                     int /* request_id */,
                     ResourceMsg_RequestCompleteData)

// Resource messages sent from the renderer to the browser.

// Makes a resource request via the browser.
IPC_MESSAGE_CONTROL3(ResourceHostMsg_RequestResource,
                    int /* routing_id */,
                    int /* request_id */,
                    ResourceHostMsg_Request)

// Cancels a resource request with the ID given as the parameter.
IPC_MESSAGE_CONTROL1(ResourceHostMsg_CancelRequest,
                     int /* request_id */)

// Follows a redirect that occured for the resource request with the ID given
// as the parameter.
IPC_MESSAGE_CONTROL1(ResourceHostMsg_FollowRedirect,
                     int /* request_id */)

// Makes a synchronous resource request via the browser.
IPC_SYNC_MESSAGE_ROUTED2_1(ResourceHostMsg_SyncLoad,
                           int /* request_id */,
                           ResourceHostMsg_Request,
                           content::SyncLoadResult)

// Sent when the renderer process is done processing a DataReceived
// message.
IPC_MESSAGE_CONTROL1(ResourceHostMsg_DataReceived_ACK,
                     int /* request_id */)

// Sent when the renderer has processed a DataDownloaded message.
IPC_MESSAGE_CONTROL1(ResourceHostMsg_DataDownloaded_ACK,
                     int /* request_id */)

// Sent by the renderer process to acknowledge receipt of a
// UploadProgress message.
IPC_MESSAGE_CONTROL1(ResourceHostMsg_UploadProgress_ACK,
                     int /* request_id */)

// Sent when the renderer process deletes a resource loader.
IPC_MESSAGE_CONTROL1(ResourceHostMsg_ReleaseDownloadedFile,
                     int /* request_id */)

// Sent by the renderer when a resource request changes priority.
IPC_MESSAGE_CONTROL3(ResourceHostMsg_DidChangePriority,
                     int /* request_id */,
                     net::RequestPriority,
                     int /* intra_priority_value */)
