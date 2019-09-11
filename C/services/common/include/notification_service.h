#ifndef _NOTIFICATION_SERVICE_H
#define _NOTIFICATION_SERVICE_H
/*
 * Fledge notification service.
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
#include <reading.h>
#include <storage_client.h>

#define SERVICE_NAME		"Fledge Notification"
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
		void			ingestReading(Reading& reading)
					{
						m_storage->readingAppend(reading);
					};

	private:
		const std::string	m_name;
		Logger*			m_logger;
		bool			m_shutdown;
		NotificationApi*	m_api;
		ManagementClient* 	m_managerClient;
		ManagementApi*		m_managementApi;
		StorageClient		*m_storage;
		std::map<std::string, bool>
					m_registerCategories;
};
#endif
