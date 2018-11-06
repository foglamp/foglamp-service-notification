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

#include <server_http.hpp>

using namespace std;
using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

class NotificationApi {
	public:
		NotificationApi(const unsigned short port,
				const unsigned int threads);
		~NotificationApi() {};
		static		NotificationApi *getInstance();
		void		initResources();
		void		start();
		void		startServer();
		void		wait();
		void		stopServer();
		unsigned short	getListenerPort();

	private:
		static NotificationApi*		m_instance;
		HttpServer*			m_server;
		unsigned short			m_port;
		unsigned int			m_threads;
		thread*				m_thread;
};

#endif
