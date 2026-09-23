#ifndef __CLOUD_UPDATE__
#define __CLOUD_UPDATE__

#define RETRY_COUNT     (300)

/**
 * Listen cloud configuration file
 * return 0 -> success, else -> error
*/
int listen_cloud_update();

/**
 * Stop listen cloud configuration file
 * return 0 -> success, else -> error
*/
int stop_cloud_update();

/**
 * Check the switch, when the configuration is updated
*/
void update_trans_sched_state();

#endif /* __CLOUD_UPDATE__ */


