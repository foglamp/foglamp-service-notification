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
#include <csignal>
#include <sys/prctl.h>
#include <notification_service.h>
#include <notification_manager.h>
#include <notification_subscription.h>
#include <notification_queue.h>

extern int makeDaemon(void);

using namespace std;

volatile sig_atomic_t signalReceived = 0;
static NotificationService *service = NULL;

/**
 * Handle received signals
 *
 * @param    signal	The received signal
 */
static void signalHandler(int signal)
{
	signalReceived = signal;
	if (service)
	{
		// Call stop() method in notification service class
		service->stop();
	}
}

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

	// Requests the kernel to deliver SIGHUP when parent dies
	prctl(PR_SET_PDEATHSIG, SIGHUP);

	// We handle these signals, add more if needed
	std::signal(SIGHUP,  signalHandler);
	std::signal(SIGINT,  signalHandler);
	std::signal(SIGSTOP, signalHandler);
	std::signal(SIGTERM, signalHandler);

	// Instantiate the NotificationService class
	service = new NotificationService(myName);

	// Start the Notification service
	service->start(coreAddress, corePort);

	// ... Notification service runs until shutdown ...

	// Service has been stopped
	delete service;
	service = NULL;

	// Return success
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
