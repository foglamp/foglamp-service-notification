/*
 * FogLAMP notification service class
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */
#include <management_api.h>
#include <management_client.h>
#include <service_record.h>
#include <plugin_manager.h>
#include <plugin_api.h>
#include <plugin.h>
#include <logger.h>
#include <iostream>
#include <string>

#include <storage_client.h>
#include <config_handler.h>
#include <notification_service.h>
#include <notification_manager.h>
#include <notification_queue.h>
#include <notification_subscription.h>

using namespace std;

/**
 * Constructor for the NotificationService class
 *
 * This class handles all Notification server components.
 *
 * @param    myName	The notification server name
 */
NotificationService::NotificationService(const string& myName) :
					 m_name(myName),
					 m_shutdown(false)
{
	// Default to a dynamic port
	unsigned short servicePort = 0;

	// Create new logger instance
	m_logger = new Logger(myName);

	// One thread
	unsigned int threads = 1;

	// Instantiate the NotificationApi class
	m_api = new NotificationApi(servicePort, threads);

	// Set NULL for other resources
	m_managerClient = NULL;
	m_managementApi = NULL;
}

/**
 * NotificationService destructor
 */
NotificationService::~NotificationService()
{
	delete m_api;
	delete m_managerClient;
	delete m_managementApi;
	delete m_logger;
}

/**
 * Start the notification service
 * by connecting to FogLAMP core service.
 *
 * @param coreAddress	The FogLAMP core address
 * @param corePort	The FogLAMP core port
 * @return		True for success, false otherwise
 */
bool NotificationService::start(string& coreAddress,
				unsigned short corePort)
{
	// Dynamic port
	unsigned short managementPort = (unsigned short)0;

	m_logger->info("Starting Notification service '" + m_name +  "' ...");

	// Instantiate ManagementApi class
	m_managementApi = new ManagementApi(SERVICE_NAME, managementPort);
	m_managementApi->registerService(this);
	m_managementApi->start();

	// Allow time for the listeners to start before we register
	while(m_managementApi->getListenerPort() == 0)
	{
		sleep(1);
	}

	// Start the NotificationApi on service port
	m_api->start();

	// Allow time for the listeners to start before we continue
	while(m_api->getListenerPort() == 0)
	{
		sleep(1);
	}

	// Enable http API methods
	m_api->initResources();
	// Set Notification callback url prefix
	m_api->setCallBackURL();

	// Get management client
	m_managerClient = new ManagementClient(coreAddress, corePort);
	if (!m_managerClient)
	{
		m_logger->fatal("Notification service '" + m_name + \
				"' can not connect to FogLAMP at " + \
				string(coreAddress + ":" + to_string(corePort)));

		this->cleanupResources();
		return false;
	}

	// Create an empty Notification category if one doesn't exist
	DefaultConfigCategory notificationConfig(string("Notifications"), string("{}"));
	notificationConfig.setDescription("Notification services");
	if (!m_managerClient->addCategory(notificationConfig, true))
	{
		m_logger->fatal("Notification service '" + m_name + \
				"' can not connect to FogLAMP ConfigurationManager at " + \
				string(coreAddress + ":" + to_string(corePort)));

		this->cleanupResources();
		return false;
	}

	// Register this notification service with FogLAMP core
	unsigned short listenerPort = m_api->getListenerPort();
	unsigned short managementListener = m_managementApi->getListenerPort();
	ServiceRecord record(m_name,
			     "Notification",		// Service type
			     "http",			// Protocol
			     "localhost",		// Listening address
			     listenerPort,		// Service port
			     managementListener);	// Management port

	if (!m_managerClient->registerService(record))
	{
		m_logger->fatal("Unable to register service "
				"\"Notification\" for service '" + m_name + "'");

		this->cleanupResources();
		return false;
	}

	// Register NOTIFICATION_CATEGORY to FogLAMP Core
	unsigned int retryCount = 0;
	while (m_managerClient->registerCategory(NOTIFICATION_CATEGORY) == false &&
		++retryCount < 10)
	{
		sleep(2 * retryCount);
	}

	// Get Storage service
	ServiceRecord storageInfo("FogLAMP Storage");
	if (!m_managerClient->getService(storageInfo))
	{
		m_logger->fatal("Unable to find FogLAMP storage "
				"connection info for service '" + m_name + "'");

		this->cleanupResources();

		// Unregister from FogLAMP
		m_managerClient->unregisterService();

		return false;
	}
	m_logger->info("Connect to storage on %s:%d",
		       storageInfo.getAddress().c_str(),
		       storageInfo.getPort());

	// Setup StorageClient
	StorageClient storageClient(storageInfo.getAddress(),
				    storageInfo.getPort());

	// Setup NotificationManager class
	bool nLoaded = false;
	NotificationManager instances(m_name, m_managerClient, this);
	try
	{
		// Get all notification instances under Notifications
		// and load plugins defined in all notifications 
		nLoaded = instances.loadInstances();
		//instancesLoaded = true;
	}
	catch (std::exception* e)
	{
		m_logger->fatal("Notification service got an error while "
				"setting up configured instances:" + string(e->what()));
		delete e;
	}
	catch (...)
	{
		std::exception_ptr p = std::current_exception();
		string name = (p ? p.__cxa_exception_type()->name() : "null");
		m_logger->fatal("Notification service igot an error while "
				"setting up configured instances: '" + name + "'");
	}

	// Check we have loaded instances and we can continue
	if (!nLoaded)
	{
		this->cleanupResources();

		// Unregister from FogLAMP
		m_managerClient->unregisterService();

		return false;
	}

	// We have notitication instances loaded
	// (1) Start the NotificationQueue
	NotificationQueue queue(m_name);

	// (2) Register notification interest, per assetName:
	// by call Storage layer Notification API.
	NotificationSubscription subscriptions(m_name, storageClient);
	subscriptions.registerSubscriptions();

	// Notification data will be now received via NotificationApi server
	// and added into the queue for processing.

	// .... wait until shutdown ...

	// Wait for all the API threads to complete
	m_api->wait();

	// Shutdown is starting ...
	// NOTE:
	// - Notification API listener is already down.
	// - all subscriptions already unregistered
	subscriptions.unregisterSubscriptions();

	// Unregister from storage service
	m_managerClient->unregisterService();

	// Stop management API
	m_managementApi->stop();

	// Flush all data in the queue
	queue.stop();

	m_logger->info("Notification service '" + m_name + "' shutdown completed.");

	return true;
}

/**
 * Unregister notification subscriptions and
 * stop NotificationAPi listener
 */
void NotificationService::stop()
{
	m_logger->info("Stopping Notification service '" + m_name + "' ...");

	// Unregister notifications to storage service
	NotificationSubscription* subscriptions = NotificationSubscription::getInstance();
	if (subscriptions)
	{
		subscriptions->unregisterSubscriptions();
	}

	// Stop the NotificationApi
	m_api->stop();
}

/**
 * Shutdown request
 */
void NotificationService::shutdown()
{
	m_shutdown = true;
	m_logger->info("Notification service '" + m_name + "' shutdown in progress ...");

	this->stop();
}

/**
 * Cleanup resources and stop services
 */
void NotificationService::cleanupResources()
{
	this->stop();
	m_api->wait();

	m_managementApi->stop();
}

/**
 * Configuration change notification
 */
void NotificationService::configChange(const string& categoryName,
				       const string& category)
{
	NotificationManager* instance = NotificationManager::getInstance();
}

/**
 * Register a configuration category for updates
 *
 * @param    categoryName	The category to register
 */
void NotificationService::registerCategory(const string& categoryName)
{
	ConfigHandler* configHandler = ConfigHandler::getInstance(m_managerClient);
	if (configHandler)
	{
		configHandler->registerCategory(this, categoryName);
	}
}
