/*
 * Error codes
 *
 *  Created on: Jan 2, 2021
 *      Author: bastian
 */

#ifndef ERROR_H_
#define ERROR_H_

typedef enum
{
	PM_OK = 0U,
	PM_FAIL,
	DELEAYED,
	OUT_OF_BOUNDS,
	UNAVAILABLE,
	ABORT,
	NOT_NEEDED,
	TIMEOUT,

} error_code_t;

#endif /* ERROR_H_ */
