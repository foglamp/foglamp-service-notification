#ifndef _NOTIFICATION_API_H
#define _NOTIFICATION_API_H
/*
 * FogLAMP Notification service.
 *
 * Copyright (c) 2018 Massimiliano Pinto
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include "logger.h"
#include <server_http.hpp>

using namespace std;
using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

/*
 * URL for each API entry point
 */
#define RECEIVE_NOTIFICATION		"^/notification/reading/asset/([A-Za-z][a-zA-Z0-9_]*)$"
#define GET_NOTIFICATION_INSTANCES	"^/notification$"
#define ASSET_NAME_COMPONENT	1

class NotificationApi
{
	public:
		NotificationApi(const unsigned short port,
				const unsigned int threads);
		~NotificationApi();
		static		NotificationApi *getInstance();
		void		initResources();
		void		start();
		void		startServer();
		void		wait();
		void		stop();
		void		stopServer();
		unsigned short	getListenerPort();
		void		processCallback(shared_ptr<HttpServer::Response> response,
						shared_ptr<HttpServer::Request> request);
		void		getInstances(shared_ptr<HttpServer::Response> response,
                                                shared_ptr<HttpServer::Request> request);
		const std::string&
				getCallBackURL() const { return m_callBackURL; };
		void		setCallBackURL();

	private:
		void		internalError(shared_ptr<HttpServer::Response>,
					      const exception&);
		void		respond(shared_ptr<HttpServer::Response>,
					const string&);
		void		respond(shared_ptr<HttpServer::Response>,
					SimpleWeb::StatusCode,
				const string&);
		// Add asset name and data to the Readings process queue
		bool		queueNotification(const string& assetName,
						  const string& payload);

	private:
		static NotificationApi*		m_instance;
		HttpServer*			m_server;
		unsigned short			m_port;
		unsigned int			m_threads;
		thread*				m_thread;
		std::string			m_callBackURL;
		Logger*				m_logger;
};

#endif
