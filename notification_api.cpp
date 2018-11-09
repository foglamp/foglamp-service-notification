/*
 * FogLAMP notification service.
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */
#include "client_http.hpp"
#include "server_http.hpp"
#include "notification_api.h"
#include "management_api.h"
#include "logger.h"


NotificationApi* NotificationApi::m_instance = 0;

using namespace std;
using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;
using HttpClient = SimpleWeb::Client<SimpleWeb::HTTP>;

/**
 * Wrapper function for the notification POST callback API call.
 */
void notificationReceiveWrapper(shared_ptr<HttpServer::Response> response,
				shared_ptr<HttpServer::Request> request)
{
	NotificationApi* api = NotificationApi::getInstance();
	api->processCallback(response, request);
}

/**
 * Construct the singleton Notification API 
 */
NotificationApi::NotificationApi(const unsigned short port,
				 const unsigned int threads)
{
	m_port = port;
	m_threads = threads;
	m_server = new HttpServer();
	m_server->config.port = port;
	m_server->config.thread_pool_size = threads;
	NotificationApi::m_instance = this;
}

/**
 * NotificationAPi destructor
 */
NotificationApi::~NotificationApi()
{
	delete m_thread;
	delete m_server;
}
/**
 * Return the singleton instance of the NotificationAPI class
 */
NotificationApi* NotificationApi::getInstance()
{
	if (m_instance == NULL)
	{
		m_instance = new NotificationApi(0, 1);
	}
	return m_instance;
}

/**
 * Return the current listener port
 */
unsigned short NotificationApi::getListenerPort()
{
	return m_server->getLocalPort();
}

void startService()
{
	NotificationApi::getInstance()->startServer();
}

/**
 * Start the HTTP server
 */
void NotificationApi::start() {
	m_thread = new thread(startService);
}

void NotificationApi::startServer() {
	m_server->start();
}

void NotificationApi::stopServer() {
	m_server->stop();
}

/**
 * Wait for the HTTP server to shutdown
 */
void NotificationApi::wait() {
	m_thread->join();
}

/**
 * Initialise the API entry points for the common data resource and
 * the readings resource.
 */
void NotificationApi::initResources()
{       
	m_server->resource[RECEIVE_NOTIFICATION]["POST"] = notificationReceiveWrapper;
}

/**
 * Handle a exception by sendign back an internal error
 */
void NotificationApi::internalError(shared_ptr<HttpServer::Response> response,
				    const exception& ex)
{
	string payload = "{ \"Exception\" : \"";

	payload = payload + string(ex.what());
	payload = payload + "\"";

	Logger *logger = Logger::getLogger();
	logger->error("NotificationApi Internal Error: %s\n", ex.what());

	this->respond(response,
		      SimpleWeb::StatusCode::server_error_internal_server_error,
		      payload);
}

/**
 * Construct an HTTP response with the 200 OK return code using the payload
 * provided.
 *
 * @param response	The response stream to send the response on
 * @param payload	The payload to send
 */
void NotificationApi::respond(shared_ptr<HttpServer::Response> response,
			      const string& payload)
{
	*response << "HTTP/1.1 200 OK\r\nContent-Length: "
		  << payload.length() << "\r\n"
		  <<  "Content-type: application/json\r\n\r\n" << payload;
}

/**
 * Construct an HTTP response with the specified return code using the payload
 * provided.
 *
 * @param response	The response stream to send the response on
 * @param code		The HTTP esponse code to send
 * @param payload	The payload to send
 */
void NotificationApi::respond(shared_ptr<HttpServer::Response> response,
			      SimpleWeb::StatusCode code,
			      const string& payload)
{
	*response << "HTTP/1.1 " << status_code(code) << "\r\nContent-Length: "
		  << payload.length() << "\r\n"
		  <<  "Content-type: application/json\r\n\r\n" << payload;
}

/**
 * Add data provided in the payload of callback API call
 * into the notification queue.
 *
 *
 * @param response	The response stream to send the response on
 * @param request	The HTTP request
 */
void NotificationApi::processCallback(shared_ptr<HttpServer::Response> response,
				      shared_ptr<HttpServer::Request> request)
{
	try
	{
		string assetName = request->path_match[ASSET_NAME_COMPONENT];
		string payload = request->content.string();
		string responsePayload;

		// Add data to the queue
		if (queueNotification(assetName, payload))
		{
			responsePayload = "{ \"response\" : \"processed\", \"";
			responsePayload += assetName;
			responsePayload += "\" : \"data queued\" }";

			this->respond(response, responsePayload);
		}
		else
		{
			responsePayload = "{ \"error\": \"error_message\" }";
			this->respond(response,
				      SimpleWeb::StatusCode::client_error_bad_request,
				      responsePayload);
		}
	}
	catch (exception ex)
	{
		this->internalError(response, ex);
	}
}

/**
 * Add readings data of asset name into the process queue
 *
 * @param assetName	The asset name
 * @param payload	Readings data belonging to asset name
 * @return		false error, true on success
 */
bool NotificationApi::queueNotification(const string& assetName,
					const string& payload)
{
	return true;
}
