#ifndef _NOTIFICATION_SERVICE_H
#define _NOTIFICATION_SERVICE_H
/*
 * FogLAMP notification service.
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */

#include <service_handler.h>
#include <management_client.h>
#include <management_api.h>

#include <notification_api.h>

#define SERVICE_NAME		"FogLAMP Notification"
#define SERVICE_TYPE		"Notification"
#define NOTIFICATION_CATEGORY	"NOTIFICATION"

/**
 * The NotificationService class.
 */
class NotificationService : public ServiceHandler
{
	public:
		NotificationService(const std::string& name);
		~NotificationService();
		bool 			start(std::string& coreAddress,
					      unsigned short corePort);
		void 			stop();
		void			shutdown();
		void			cleanupResources();
		void			configChange(const std::string&,
						     const std::string&);
		void			registerCategory(const std::string& categoryName);

	private:
		const std::string	m_name;
		Logger*			m_logger;
		bool			m_shutdown;
		NotificationApi*	m_api;
		ManagementClient* 	m_managerClient;
		ManagementApi*		m_managementApi;
		std::map<std::string, bool>
					m_registerCategories;
};
#endif
