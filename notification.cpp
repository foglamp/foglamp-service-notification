/*
 * FogLAMP notification service.
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Massimiliano Pinto
 */
#include <notification_service.h>
#include <management_api.h>
#include <management_client.h>
#include <service_record.h>
#include <plugin_manager.h>
#include <plugin_api.h>
#include <plugin.h>
#include <logger.h>
#include <iostream>
#include <string>

extern int makeDaemon(void);

using namespace std;

/**
 * Notification service main entry point
 */
int main(int argc, char *argv[])
{
	unsigned short corePort = 8082;
	string	       coreAddress = "localhost";
	bool	       daemonMode = true;
	string	       myName = SERVICE_NAME;

	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "-d"))
		{
			daemonMode = false;
		}
		else if (!strncmp(argv[i], "--port=", 7))
		{
			corePort = (unsigned short)atoi(&argv[i][7]);
		}
		else if (!strncmp(argv[i], "--name=", 7))
		{
			myName = &argv[i][7];
		}
		else if (!strncmp(argv[i], "--address=", 10))
		{
			coreAddress = &argv[i][10];
		}
	}

	if (daemonMode && makeDaemon() == -1)
	{
		// Failed to run in daemon mode
		cout << "Failed to run as deamon - "
			"proceeding in interactive mode." << endl;
	}

	// Instantiate the NotificationService class
	NotificationService *service = new NotificationService(myName);

	// Start the Notification service
	service->start(coreAddress, corePort);

	delete service;

	return 0;
}

/**
 * Detach the process from the terminal and run in the background.
 *
 * @return	-1 in case of errors and 0 on success.
 */
int makeDaemon()
{
	pid_t pid;

	/* create new process */
	if ((pid = fork()  ) == -1)
	{
		return -1;  
	}
	else if (pid != 0)  
	{
		exit (EXIT_SUCCESS);  
	}

	// If we got here we are a child process

	// create new session and process group 
	if (setsid() == -1)  
	{
		return -1;  
	}

	// Close stdin, stdout and stderr
	close(0);
	close(1);
	close(2);
	// redirect fd's 0,1,2 to /dev/null
	(void)open("/dev/null", O_RDWR);    // stdin
	(void)dup(0);  			    // stdout	GCC bug 66425 produces warning
	(void)dup(0);  			    // stderr	GCC bug 66425 produces warning

 	return 0;
}

/**
 * Constructor for the notification service
 */
NotificationService::NotificationService(const string& myName) : m_name(myName),
								 m_shutdown(false)
{
	unsigned short servicePort;

	m_logger = new Logger(myName);

	// Default to a dynamic port
	servicePort = 0;

	// One thread
	unsigned int threads = 1;

	// Instantiate the NotificationApi class
	m_api = new NotificationApi(servicePort, threads);
}

// Destructor
NotificationService::~NotificationService()
{
	delete m_api;
}

/**
 * Start the notification service
 * by connecting to FogLAMP core service.
 *
 * @param coreAddress	The FogLAMP core address
 * @param corePort	The FogLAMP core port
 */
void NotificationService::start(string& coreAddress,
				unsigned short corePort)
{
	m_logger->info("Starting Notification service " + m_name +  " ...");

	// Start the NotificationApi on service port
	// Dynamic port
	unsigned short managementPort = (unsigned short)0;
	ManagementApi management(SERVICE_NAME, managementPort);
	management.registerService(this);
	management.start();

	// Start the NotificationApi on service port
	m_api->start();

	// Enable http API methods
	m_api->initResources();

	// Allow time for the listeners to start before we register
	sleep(1);

	if (!m_shutdown)
	{
		// Get management client
		ManagementClient *client = new ManagementClient(coreAddress, corePort);

		// Create an empty Notification category if one doesn't exist
		DefaultConfigCategory notificationConfig(string("Notifications"), string("{}"));
		notificationConfig.setDescription("Notification services");
		client->addCategory(notificationConfig, true);

		unsigned short listenerPort = m_api->getListenerPort();
		unsigned short managementListener = management.getListenerPort();

		// Register this notification service with FogLAMP core
		ServiceRecord record(m_name,
				     "Notification",
				     "http",
				     "localhost",
				     listenerPort,
				     managementListener);

		if (!client->registerService(record))
		{
			delete client;
			delete m_api;
			return;
		}
	

		unsigned int retryCount = 0;
		while (client->registerCategory(NOTIFICATION_CATEGORY) == false && ++retryCount < 10)
		{
			sleep(2 * retryCount);
		}

		// Wait for all the API threads to complete
		m_api->wait();

		// Clean shutdown, unregister the notification service
		client->unregisterService();
	}
	else
	{
		// Wait for all the API threads to complete
		m_api->wait();
	}

	m_logger->info("Notification service [" + m_name + " shutdown completed.");
}

/**
 * Stop the notification service/
 */
void NotificationService::stop()
{
	m_logger->info("Stopping service...\n");
}

/**
 * Shutdown request
 */
void NotificationService::shutdown()
{
	/* Stop recieving new requests and allow existing
	 * requests to drain.
	 */
	m_shutdown = true;
	m_logger->info("Notification service shutdown in progress.");
}

/**
 * Configuration change notification
 */
void NotificationService::configChange(const string& categoryName,
				       const string& category)
{
}
