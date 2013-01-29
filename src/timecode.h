/* libtimecode -- timecode.h compatible */

#include <stddef.h>
#include <inttypes.h>
#include <stdint.h>

#if (!defined int32_t && !defined __int8_t_defined && !defined _INT32_T)
typedef int int32_t;
#endif

#if (!defined int64_t && !defined __int8_t_defined && !defined _UINT64_T)
#  if __WORDSIZE == 64
typedef long int int64_t;
#else
typedef long long int int64_t;
#endif
#endif

/**
 * classical timecode
 */
typedef struct TimecodeTime {
	int32_t hour; ///< timecode hours 0..24
	int32_t minute; ///< timecode minutes 0..59
	int32_t second; ///< timecode seconds 0..59
	int32_t frame; ///< timecode frames 0..fps
	int32_t subframe; ///< timecode subframes 0..
} TimecodeTime;


/**
 * define a frame rate
 */
typedef struct TimecodeRate {
	int32_t num; ///< fps numerator
	int32_t den; ///< fps denominator
	int drop; ///< 1: use drop-frame timecode (only valid for 2997/100)
	int32_t subframes; ///< number of subframes per frame
} TimecodeRate;


double timecode_rate_to_double(TimecodeRate const * const r);
void timecode_time_to_string (char *smptestring, TimecodeTime const * const t);
void timecode_framenumber_to_time (TimecodeTime * const t, TimecodeRate const * const r, const int64_t frameno);

void timecode_framenumber_to_string (char *smptestring, TimecodeRate const * const r, const int64_t frameno);
