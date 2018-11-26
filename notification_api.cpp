/*
 * FogLAMP Notification API class for Notification micro service.
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
#include "notification_manager.h"
#include "notification_queue.h"


NotificationApi* NotificationApi::m_instance = 0;

using namespace std;
using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;
using HttpClient = SimpleWeb::Client<SimpleWeb::HTTP>;

/**
 * Wrapper function for the notification POST callback API call.
 *
 * @param response	The response stream to send the response on
 * @param request	The HTTP request
 */
void notificationReceiveWrapper(shared_ptr<HttpServer::Response> response,
				shared_ptr<HttpServer::Request> request)
{
	NotificationApi* api = NotificationApi::getInstance();
	api->processCallback(response, request);
}

/**
 * Wrapper for GET /notification
 * Reply to caller with a JSON string of all loaded Notification instances
 *
 * @param response	The response stream to send the response on
 * @param request	The HTTP request
 */
void notificationGetInstances(shared_ptr<HttpServer::Response> response,
			      shared_ptr<HttpServer::Request> request)
{
	NotificationApi* api = NotificationApi::getInstance();
	api->getInstances(response, request);
}
/**
 * Construct the singleton Notification API
 *
 * @param    port	Listening port (0 = automatically set)
 * @param    threads	Thread pool size of HTTP server
 */
NotificationApi::NotificationApi(const unsigned short port,
				 const unsigned int threads)
{
	m_port = port;
	m_threads = threads;
	m_server = new HttpServer();
	m_server->config.port = port;
	m_server->config.thread_pool_size = threads;
	m_thread = NULL;
	m_callBackURL = "";

	NotificationApi::m_instance = this;
}

/**
 * NotificationAPi destructor
 */
NotificationApi::~NotificationApi()
{
	this->stopServer();
	m_thread->join();

	delete m_server;
	delete m_thread;
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
 *
 * @return	The current listener port
 */
unsigned short NotificationApi::getListenerPort()
{
	return m_server->getLocalPort();
}

/**
 * Method for HTTP server, called by a dedicated thread
 */
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

/**
 * Start method for HTTP server
 */
void NotificationApi::startServer() {
	m_server->start();
}

/**
 * Stop method for HTTP server
 */
void NotificationApi::stopServer() {
	m_server->stop();
}

/**
 * API stop entery poiunt
 */
void NotificationApi::stop()
{
	this->stopServer();
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
	m_server->resource[GET_NOTIFICATION_INSTANCES]["GET"] = notificationGetInstances;
}

/**
 * Handle a exception by sendign back an internal error
 *
  *
 * @param response	The response stream to send the response on.
 * @param ex		The current exception caught.
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
 * This is called by the storage service when new data arrives
 * for an asset in which we have registered an interest.
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
	ReadingSet* readings = NULL;
	try
	{
		readings = new ReadingSet(payload);
	}
	catch (exception* ex)
	{
		Logger::getLogger()->error("Exception '" + string(ex->what()) + \
					   "' while parsing readigns for asset '" + \
					   assetName + "'" );
		delete ex;
		return false;
	}
	catch (...)
	{
		std::exception_ptr p = std::current_exception();
		string name = (p ? p.__cxa_exception_type()->name() : "null");
		Logger::getLogger()->error("Exception '" + name + \
					   "' while parsing readigns for asset '" + \
					   assetName  + "'" );
		return false;
	}

	NotificationQueue* queue = NotificationQueue::getInstance();
	NotificationQueueElement* item =  new NotificationQueueElement(assetName, readings);

	// Add element to the queue
	return queue->addElement(item);
}

/**
 * Return JSON string of all loaded instances
 * @param response	The response stream to send the response on
 * @param request	The HTTP request
 */
void NotificationApi::getInstances(shared_ptr<HttpServer::Response> response,
				   shared_ptr<HttpServer::Request> request)
{
	string responsePayload;
	// Get NotificationManager instance
	NotificationManager* manager = NotificationManager::getInstance();
	if (manager)
	{
		// Get all Notification instances
		responsePayload = "{ \"notifications\": [" + manager->getJSONInstances()  + "] }";
		this->respond(response,
			      responsePayload);
	}
	else
	{
		responsePayload = "{ \"error\": \"NotificationManager not yet available.\" }";
		this->respond(response,
			      SimpleWeb::StatusCode::server_error_internal_server_error,
			      responsePayload);
	}
}

/**
 * Set the callBack URL prefix for Notification callbacks.
 */
void NotificationApi::setCallBackURL()
{
	unsigned short apiPort =  this->getListenerPort();
	m_callBackURL = "http://127.0.0.1:" + to_string(apiPort) + "/notification/reading/asset/";

	Logger::getLogger()->debug("Notification service: callBackURL prefix is " + m_callBackURL);
}
