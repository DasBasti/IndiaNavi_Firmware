#include <stddef.h>

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

char *readline(char *source, char *destination)
{
	if (source == 0)
		return 0;

	do
	{
		if (*source == 0)
		{
			*destination = 0;
			return 0;
		}
		if (*source == '\n')
			break;
		if (*source == '\r')
			continue;
		*destination = *source;
		destination++;
	} while (source++);
	*destination = 0;
	return ++source;
}