#include <gtest/gtest.h>
#include "notification_service.h"
#include "notification_manager.h"
#include "notification_queue.h"

using namespace std;

TEST(NotificationService, AddInstance)
{
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

	// Check state is still StateCleared and rert is false (notification can not be sent)
	ASSERT_TRUE(instance->getState() == NotificationInstance::StateCleared && !ret);

	// We have onky two toggled messages to send, within repeat frequency period
	ASSERT_EQ(toggled, 2);

	// Remove this instance
	if (instance)
	{
		delete instance;
	}

	// NotificationType is ONE SHOT
	nType.type = E_NOTIFICATION_TYPE::OneShot;
	instance = new NotificationInstance("OneShot",
					    false,
					    nType,
					    NULL,
					    NULL);

	ret = instance->handleState(true);
	ASSERT_TRUE(instance->getState() == NotificationInstance::StateTriggered && ret);
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
	ASSERT_EQ(oneshot, 1);

	ret = instance->handleState(true);
	ASSERT_TRUE(instance->getState() == NotificationInstance::StateTriggered && !ret);

	ret = instance->handleState(false);
	ASSERT_TRUE(instance->getState() == NotificationInstance::StateCleared && !ret);

	ret = instance->handleState(true);
	ASSERT_TRUE(instance->getState() == NotificationInstance::StateCleared && !ret);

	// Remove this instance
	if (instance)
	{
		delete instance;
	}

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
	ASSERT_TRUE(instance->getState() == NotificationInstance::StateTriggered && ret);

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
	ASSERT_TRUE(instance->getState() == NotificationInstance::StateTriggered && !ret);

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
	ASSERT_TRUE(instance->getState() == NotificationInstance::StateCleared && !ret);
	ASSERT_EQ(retriggered, 1);

	// Loop with evaluations set to true
	for (int i = 0; i < 10; i++)
	{
		ret = instance->handleState(true);
	}

	// Do not send notifications, within repeat frequency period
	ASSERT_TRUE(instance->getState() == NotificationInstance::StateTriggered && !ret);

	// Remove this instance
	if (instance)
	{
		delete instance;
	}

}
