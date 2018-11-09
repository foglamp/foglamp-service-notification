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
