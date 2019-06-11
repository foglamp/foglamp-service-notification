#include <gtest/gtest.h>
#include "notification_service.h"
#include "notification_manager.h"
#include "notification_queue.h"

using namespace std;

TEST(NotificationService, AddInstance)
{
EXPECT_EXIT({
	bool ret = false;
	long toggled = 0;
	long oneshot = 0;
	long retriggered = 0;
	NotificationInstance* instance = NULL;
	NOTIFICATION_TYPE nType;
	nType.type = E_NOTIFICATION_TYPE::Toggled;
	nType.retriggerTime = DEFAULT_RETRIGGER_TIME;

	// NotificationType is TOGGLED
	instance = new NotificationInstance("Toggled",
					    false,
					    nType,
					    NULL,
					    NULL);
	for (int i = 0; i < 10; i++)
	{
		ret = instance->handleState(true);
		if (instance->getState() == NotificationInstance::StateTriggered && ret)
		{
			toggled++;
		}
		ret = instance->handleState(false);
		if (instance->getState() == NotificationInstance::StateCleared && ret)
		{
			toggled++;
		}
	}

	// This doesn't change the state within repeat frequency period
	ret = instance->handleState(true);

	// Check state is still StateCleared and ret is false (notification can not be sent)
	bool testStatus = instance->getState() == NotificationInstance::StateCleared && !ret;
	if (!testStatus)
	{
		cerr << "Toggled Notification should not be sent" << endl;
		delete instance;
		exit(1);
	}

	// We have onky two toggled messages to send, within repeat frequency period
	testStatus = toggled == 2;

	if (!testStatus)
	{
		cerr << "We have " << toggled << " toggled messages instead of 2" << endl;
		delete instance;
		exit(1);
	}

	// Remove this instance
	delete instance;

	// NotificationType is ONE SHOT
	nType.type = E_NOTIFICATION_TYPE::OneShot;
	instance = new NotificationInstance("OneShot",
					    false,
					    nType,
					    NULL,
					    NULL);

	ret = instance->handleState(true);
	testStatus = instance->getState() == NotificationInstance::StateTriggered && ret;
	if (!testStatus)
	{
		cerr << "OneShot Notification should be sent" << endl;
		delete instance;
		exit(1);
	}

	if (instance->getState() == NotificationInstance::StateTriggered && ret)
	{
		oneshot++;
	}

	for (int i = 0; i < 10; i++)
	{
		ret = instance->handleState(true);
		if (instance->getState() == NotificationInstance::StateTriggered && ret)
		{
			oneshot++;
		}
	}
	testStatus = oneshot == 1;
	if (!testStatus)
	{
		delete instance;
		cerr << "We have " << oneshot << " oneshot messages instead of 1" << endl;
		exit(1);
	}

	ret = instance->handleState(true);
	testStatus = instance->getState() == NotificationInstance::StateTriggered && !ret;
	if (!testStatus)
	{
		cerr << "OneShot Notification should not be sent" << endl;
		delete instance;
		exit(1);
	}

	ret = instance->handleState(false);
	testStatus = instance->getState() == NotificationInstance::StateCleared && !ret;
	if (!testStatus)
	{
		cerr << "OneShot Notification should not be sent" << endl;
		delete instance;
		exit(1);
	}

	ret = instance->handleState(true);
	testStatus = instance->getState() == NotificationInstance::StateCleared && !ret;
	if (!testStatus)
	{
		cerr << "OneShot Notification should not be sent" << endl;
		delete instance;
		exit(1);
	}

	// Remove this instance
	delete instance;

	// NotificationType is retriggered
	nType.type = E_NOTIFICATION_TYPE::Retriggered;
	instance = new NotificationInstance("Retriggered",
					    false,
					    nType,
					    NULL,
					    NULL);
	ret = instance->handleState(true);
	if (ret)
	{
		retriggered++;
	}
	// First time StateTriggered, send Notification
	testStatus = instance->getState() == NotificationInstance::StateTriggered && ret;
	if (!testStatus)
	{
		cerr << "Retriggered Notification should be sent" << endl;
		delete instance;
		exit(1);
	}

	// Loop with evaluations set to true
	for (int i = 0; i < 10; i++)
	{
		ret = instance->handleState(true);
		if (ret)
		{
			retriggered++;
		}
	}
	// Do not send notifications, within repeat frequency period
	testStatus = instance->getState() == NotificationInstance::StateTriggered && !ret;
	if (!testStatus)
	{
		cerr << "Retriggered Notification should not be sent" << endl;
		delete instance;
		exit(1);
	}

	// Check result after some some evaluations set to true and false
	for (int i = 0; i < 10; i++)
	{
		ret = instance->handleState(true);
		if (ret)
		{
			retriggered++;
		}
		ret = instance->handleState(false);
		if (ret)
		{
			retriggered++;
		}
	}

	// Final state after loop is StateCleared
	testStatus = instance->getState() == NotificationInstance::StateCleared && !ret;
	if (!testStatus)
	{
		cerr << "Retriggered Notification should not be sent" << endl;
		delete instance;
		exit(1);
	}
	testStatus = retriggered == 1;	
	if (!testStatus)
	{
		cerr << "We have " << retriggered << " retriggered messages instead of 1" << endl;
		delete instance;
		exit(1);
	}

	// Loop with evaluations set to true
	for (int i = 0; i < 10; i++)
	{
		ret = instance->handleState(true);
	}

	// Do not send notifications, within repeat frequency period
	testStatus = instance->getState() == NotificationInstance::StateTriggered && !ret;
	if (!testStatus)
	{
		cerr << "Retriggered Notification should not be sent" << endl;
		delete instance;
		exit(1);
	}

	// Remove this instance
	delete instance;

	exit(!(testStatus == true)); }, ::testing::ExitedWithCode(0), "");
}
