#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t GdiplusVersion; int SuppressExternalCodecs; } GdiplusStartupInput;
typedef struct { uint32_t HookSize; void* Callback; void* Unhook; } GdiplusStartupOutput;

int GdiplusStartup(uint64_t *token, GdiplusStartupInput *input, GdiplusStartupOutput *output) {
    if (token) *token = 1;
    return 0;
}
void GdiplusShutdown(uint64_t token) {}
int GdipCreateFromContext(void* context, float w, float h, void** graphics) { *graphics = malloc(64); return 0; }
int GdipCreateFromContext_mono(void* context, float w, float h, void** graphics) { *graphics = malloc(64); return 0; }
int GdipCreateBitmapFromScan0(int w, int h, int stride, int pf, void* sd, void** bmp) { *bmp = malloc(64); return 0; }
int GdipCreateBitmapFromFile(void* file, void** bmp) { *bmp = malloc(64); return 0; }
int GdipCreateHBITMAPFromBitmap(void* bmp, void* hbm, uint32_t bg) { return 0; }
int GdipDisposeImage(void* img) { free(img); return 0; }
int GdipGetImageWidth(void* img, uint32_t* w) { *w = 64; return 0; }
int GdipGetImageHeight(void* img, uint32_t* h) { *h = 64; return 0; }
int GdipGetImageGraphicsContext(void* img, void** g) { *g = malloc(64); return 0; }
int GdipGraphicsClear(void* g, uint32_t color) { return 0; }
int GdipDrawImageRectI(void* g, void* img, int x, int y, int w, int h) { return 0; }
int GdipDrawString(void* g, void* str, int len, void* font, void* rect, void* fmt, void* brush) { return 0; }
int GdipMeasureString(void* g, void* str, int len, void* font, void* rect, void* fmt, void* bbox, int* chars, int* lines) { return 0; }
int GdipCreateSolidFill(uint32_t color, void** brush) { *brush = malloc(64); return 0; }
int GdipCreateFont(void* family, float em, int style, int unit, void** font) { *font = malloc(64); return 0; }
int GdipCreateFontFromLogfontW(void* hdc, void* lf, void** font) { *font = malloc(64); return 0; }
int GdipDeleteFont(void* f) { free(f); return 0; }
int GdipDeleteBrush(void* b) { free(b); return 0; }
int GdipDeleteGraphics(void* g) { free(g); return 0; }
int GdipDeleteStringFormat(void* f) { free(f); return 0; }
int GdipDisposeImageAttributes(void* a) { free(a); return 0; }
int GdipCreateStringFormat(int flags, int lang, void** fmt) { *fmt = malloc(64); return 0; }
int GdipSetStringFormatFlags(void* fmt, int flags) { return 0; }
int GdipSetStringFormatAlign(void* fmt, int align) { return 0; }
int GdipGetStringFormatAlign(void* fmt, int* align) { *align = 0; return 0; }
int GdipCloneStringFormat(void* fmt, void** clone) { *clone = malloc(64); return 0; }
int GdipGetFamilyName(void* family, void* name, int lang) { return 0; }
int GdipCreateFontFamilyFromName(void* name, void* collection, void** family) { *family = malloc(64); return 0; }
int GdipDeleteFontFamily(void* f) { free(f); return 0; }
int GdipGetFontStyle(void* font, int* style) { *style = 0; return 0; }
int GdipGetFontSize(void* font, float* size) { *size = 12.0f; return 0; }
int GdipGetFontUnit(void* font, int* unit) { *unit = 0; return 0; }
int GdipGetFontHeight(void* font, void* g, float* h) { *h = 16.0f; return 0; }
int GdipGetLineSpacing(void* family, int style, float* spacing) { *spacing = 20.0f; return 0; }
int GdipGetCellAscent(void* family, int style, uint16_t* a) { *a = 16; return 0; }
int GdipGetCellDescent(void* family, int style, uint16_t* d) { *d = 4; return 0; }
int GdipFillRectangle(void* g, void* brush, float x, float y, float w, float h) { return 0; }
int GdipFillRectangleI(void* g, void* brush, int x, int y, int w, int h) { return 0; }
int GdipSetWorldTransform(void* g, void* matrix) { return 0; }
int GdipResetWorldTransform(void* g) { return 0; }
int GdipCreateMatrix(void** m) { *m = malloc(64); return 0; }
int GdipCreateMatrix2(float a, float b, float c, float d, float e, float f, void** m) { *m = malloc(64); return 0; }
int GdipDeleteMatrix(void* m) { free(m); return 0; }
int GdipSetClipRectI(void* g, int x, int y, int w, int h, int mode) { return 0; }
int GdipSetInterpolationMode(void* g, int mode) { return 0; }
int GdipSetPageUnit(void* g, int unit) { return 0; }
int GdipSetSmoothingMode(void* g, int mode) { return 0; }
int GdipSetTextRenderingHint(void* g, int mode) { return 0; }
int GdipDrawImageRectRectI(void* g, void* img, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, int srcUnit, void* ia, void* cb, void* cbData) { return 0; }
int GdipLoadImageFromFile(void* file, void** img) { *img = malloc(64); return 0; }
int GdipLoadImageFromFileICM(void* file, void** img) { *img = malloc(64); return 0; }
int GdipSaveImageToFile(void* img, void* file, void* clsid, void* params) { return 0; }
int GdipCreateImageAttributes(void** ia) { *ia = malloc(64); return 0; }
int GdipSetImageAttributesColorMatrix(void* ia, int flags, int enable, void* matrix, void* grayMatrix, int flags2) { return 0; }
int GdipGetImagePixelFormat(void* img, int* pf) { *pf = 0x26200a; return 0; }
int GdipBitmapLockBits(void* bmp, void* rect, int flags, int pf, void* bd) { return 0; }
int GdipBitmapUnlockBits(void* bmp, void* bd) { return 0; }
int GdipFlush(void* g, int intention) { return 0; }
int GdipGetDpiX(void* g, float* dpi) { *dpi = 96.0f; return 0; }
int GdipGetDpiY(void* g, float* dpi) { *dpi = 96.0f; return 0; }
int GdipTranslateWorldTransform(void* g, float dx, float dy, int order) { return 0; }
int GdipScaleWorldTransform(void* g, float sx, float sy, int order) { return 0; }
int GdipRotateWorldTransform(void* g, float angle, int order) { return 0; }
int GdipGetTransform(void* g, void* m) { return 0; }
int GdipIsStyleAvailable(void* family, int style, int* avail) { *avail = 1; return 0; }
int GdipGetEmHeight(void* family, int style, uint16_t* h) { *h = 2048; return 0; }
int GdipFillPolygon(void* g, void* brush, void* points, int count, int fillMode) { return 0; }
int GdipDrawLines(void* g, void* pen, void* points, int count) { return 0; }
int GdipCreatePen1(uint32_t color, float width, int unit, void** pen) { *pen = malloc(64); return 0; }
int GdipDeletePen(void* p) { free(p); return 0; }
int GdipDrawLine(void* g, void* pen, float x1, float y1, float x2, float y2) { return 0; }
int GdipDrawArc(void* g, void* pen, float x, float y, float w, float h, float start, float sweep) { return 0; }
int GdipFillEllipse(void* g, void* brush, float x, float y, float w, float h) { return 0; }
int GdipDrawRectangle(void* g, void* pen, float x, float y, float w, float h) { return 0; }
int GdipFillPath(void* g, void* brush, void* path) { return 0; }
int GdipDrawPath(void* g, void* pen, void* path) { return 0; }
int GdipCreatePath(int fillMode, void** path) { *path = malloc(64); return 0; }
int GdipDeletePath(void* p) { free(p); return 0; }
int GdipAddPathRectangle(void* p, float x, float y, float w, float h) { return 0; }
int GdipCloneImage(void* img, void** clone) { *clone = malloc(64); return 0; }
int GdipGetImagePalette(void* img, void* pal, int size) { return 0; }
int GdipGetImagePaletteSize(void* img, int* size) { *size = 0; return 0; }
int GdipSetImagePalette(void* img, void* pal) { return 0; }
