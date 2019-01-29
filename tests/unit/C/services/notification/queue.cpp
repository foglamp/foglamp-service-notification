#include <gtest/gtest.h>
#include "notification_service.h"
#include "notification_manager.h"
#include "notification_queue.h"

using namespace std;

/**
 * Notification service main entry point
 */
TEST(NotificationService, Queue)
{
	string myName = "myName";

	NotificationApi* api = new NotificationApi(0, 1);
	api->setCallBackURL();
	//ASSERT_EQ(false, api->removeNotification("AAA"));

	NotificationQueue* queue = new NotificationQueue(myName);
	api->queueNotification("PIPPO", "{\"readings\" : []}");

	// Allow queue to start
	sleep(1);

	api->stop();
        queue->stop();

	delete queue;
	delete api;
}
