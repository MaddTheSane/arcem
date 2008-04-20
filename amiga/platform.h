#include <intuition/intuition.h>
#include <proto/icon.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/asl.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
//#include <proto/input.h>
//#include <proto/picasso96api.h>

struct Library *ExecBase;
struct ExecIFace *IExec;
struct Library *IntuitionBase;
struct IntuitionIFace *IIntuition;
//struct Library *P96Base;
//struct P96IFace *IP96;
struct Library *GfxBase;
struct GraphicsIFace *IGraphics;
struct Library *DOSBase;
struct DOSIFace *IDOS;
struct Library *AslBase;
struct AslIFace *IAsl;
struct Library *IconBase;
struct IconIFace *IIcon;
struct Library *UtilityBase;
struct UtilityIFace *IUtility;
//struct Device *inputdevice;

extern int force8bit;

extern void cleanup(void);
extern void sound_exit(void);

struct XColor
{
	ULONG red;
	ULONG green;
	ULONG blue;
};

  struct {
    char *ImageData,*CursorImageData;

    /* Map from host memory contents to 'pixel' value for putpixel */
    unsigned long pixelMap[256];

    /* Map from host memory contents to 'pixel' value for putpixel in cursor*/
    unsigned long cursorPixelMap[4];
    int red_shift,red_prec;
    int green_shift,green_prec;
    int blue_shift,blue_prec;

	struct Window *window;

  } HostDisplay;