#include <stddef.h>
#include <stdint.h>

size_t countline(char *source)
{
	size_t count = 0;
	if (source == 0)
		return 0;
	do
	{
		if (*source == 0)
		{
			return 0;
		}
		if (*source == '\n')
			break;
		if (*source == '\r')
			continue;
		count++;
	} while (source++);
	return ++count;
}

/*
 * Copy one line from source into destination.
 *
 * At most size - 1 characters are copied, the rest of the line is skipped.
 * destination is always \0 terminated (if size > 0).
 *
 * returns the start of the next line or NULL if the end of source is reached.
 */
char *readline_n(char *source, char *destination, size_t size)
{
	size_t len = 0;

	if (source == 0)
		return 0;
	if (destination == 0 || size == 0)
		return 0;

	for (;; source++)
	{
		if (*source == 0)
		{
			destination[len] = 0;
			return 0;
		}
		if (*source == '\n')
			break;
		if (*source == '\r')
			continue;
		if (len < size - 1)
			destination[len++] = *source;
	}
	destination[len] = 0;
	return ++source;
}

char *readline(char *source, char *destination)
{
	return readline_n(source, destination, SIZE_MAX);
}
