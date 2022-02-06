#include <stdio.h>
#include <jpeglib.h>

int main()
{
	struct jpeg_error_mgr jerr;
	jpeg_std_error(&jerr);
	return 0;
}
