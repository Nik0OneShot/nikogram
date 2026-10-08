#include "Loader.h"
#include "resource.h"
#include <objidl.h>
#include <gdiplus.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <thread>
#include <memory>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <windowsx.h>

using namespace Gdiplus;
namespace {
constexpr int Width=760,Height=422;
constexpr UINT ResultMessage=WM_APP+1;
enum Control { Inject=1002,Keep,ExtractDll,ExtractSource,Minimize,Close,Notices,StartupMenu };
const Color Bg(255,23,18,29),Panel(255,33,25,39),Line(255,67,51,78),Text(255,241,233,247),Muted(255,185,167,200),Accent(255,197,161,236),Gold(255,255,219,146);
struct App {
 HWND window=nullptr,inject=nullptr,keep=nullptr,dll=nullptr,source=nullptr,minimize=nullptr,close=nullptr,notices=nullptr,menu=nullptr,menuLabel=nullptr;
 HINSTANCE instance=nullptr;HFONT font=nullptr;HBRUSH brush=nullptr;float scale=1;
 std::unique_ptr<Image> logo;IStream* logoStream=nullptr;
 loader::Target target;bool busy=false,loaded=false;
 ~App(){logo.reset();if(logoStream)logoStream->Release();if(font)DeleteObject(font);if(brush)DeleteObject(brush);}
 int Px(int value)const{return int(value*scale+.5f);}
}app;

void Label(Graphics& g,const wchar_t* text,RectF rect,float size,Color color,bool center=false){
 FontFamily family(L"Segoe UI");Font font(&family,size,FontStyleRegular,UnitPixel);SolidBrush brush(color);
 StringFormat format;format.SetLineAlignment(StringAlignmentCenter);format.SetAlignment(center?StringAlignmentCenter:StringAlignmentNear);
 format.SetFormatFlags(StringFormatFlagsNoWrap);format.SetTrimming(StringTrimmingEllipsisCharacter);
 g.DrawString(text,-1,&font,rect,&format,&brush);
}
void Box(Graphics& g,RectF r,Color fill,Color border){SolidBrush b(fill);Pen p(border,1);g.FillRectangle(&b,r);g.DrawRectangle(&p,r);}
void ButtonPaint(Graphics& g,RectF rect,int id,const wchar_t* title,bool enabled,bool pressed,bool focus){
 Color fill=id==Inject?(enabled?Accent:Color(255,52,41,63)):Panel;
 Color ink=id==Inject&&enabled?Color(255,36,19,49):Text;
 if(!enabled)ink=Muted;if(pressed&&enabled)fill=Color(255,114,86,143);
 Box(g,RectF(rect.X+.5f,rect.Y+.5f,rect.Width-1,rect.Height-1),fill,id==Inject&&enabled?Color(255,223,196,250):Line);
 Label(g,title,rect,id==Notices?11.f:14.f,ink,true);
 if(focus){Pen pen(Gold,1);pen.SetDashStyle(DashStyleDot);g.DrawRectangle(&pen,rect.X+3,rect.Y+3,rect.Width-6,rect.Height-6);}
}
void Paint(Graphics& g){
 g.Clear(Bg);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
 SolidBrush bar(Color(255,29,22,36));g.FillRectangle(&bar,0,0,Width,33);
 Label(g,L"nikogram",RectF(16,0,500,33),12,Muted);
 Pen line(Line,1);g.DrawLine(&line,0,33,Width,33);
 LinearGradientBrush hero(Point(0,34),Point(0,181),Bg,Color(255,38,26,49));g.FillRectangle(&hero,0,34,Width,147);
 if(app.logo)g.DrawImage(app.logo.get(),RectF(155,35,450,150));
 SolidBrush dot(app.target.ready?Gold:Muted);g.FillEllipse(&dot,24,207,7,7);
 Label(g,app.busy?L"loading nikogram...":app.loaded?L"DLL loaded; check Nikogram in-game":app.target.status.c_str(),RectF(40,195,470,31),14,Text);
 g.DrawLine(&line,554,200,554,366);
 g.DrawLine(&line,0,389,Width,389);
 Label(g,L"offline  /  x64",RectF(16,390,170,31),11,Muted);
 Label(g,L"moonlit build  /  source included",RectF(350,390,310,31),11,Muted);
}
void LoadLogo(){auto data=loader::Resource(ID_LOGO);app.logoStream=SHCreateMemStream(data.data(),UINT(data.size()));if(app.logoStream)app.logo.reset(Image::FromStream(app.logoStream));}
void SizeControls(){
 auto place=[](HWND h,int x,int y,int w,int height){MoveWindow(h,app.Px(x),app.Px(y),app.Px(w),app.Px(height),TRUE);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(app.font),TRUE);};
 if(app.font)DeleteObject(app.font);
 app.font=CreateFontW(-app.Px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
 place(app.menuLabel,24,237,125,26);place(app.menu,155,233,375,180);
 place(app.inject,24,285,506,40);place(app.keep,24,335,300,28);
 place(app.dll,575,235,161,40);place(app.source,575,285,161,40);
 place(app.minimize,676,0,38,32);place(app.close,719,0,40,32);place(app.notices,675,394,65,23);
 InvalidateRect(app.window,nullptr,TRUE);
}
void Refresh(){
 if(app.busy)return;
 auto next=loader::Detect();
 if(next.ready)KillTimer(app.window,2);
 app.loaded=false;app.target=std::move(next);EnableWindow(app.inject,app.target.ready);InvalidateRect(app.window,nullptr,FALSE);
}
void Extract(bool source){
 wchar_t path[32768];wcscpy_s(path,source?L"Nikogram-source-bundle.zip":L"Nikogram-Public-x64-Release.dll");
 OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=app.window;dialog.lpstrFile=path;dialog.nMaxFile=32768;
 dialog.lpstrFilter=source?L"Source archive (*.zip)\0*.zip\0\0":L"Nikogram DLL (*.dll)\0*.dll\0\0";
 dialog.lpstrDefExt=source?L"zip":L"dll";dialog.lpstrTitle=source?L"Extract matching Nikogram source":L"Extract embedded Nikogram DLL";
 dialog.Flags=OFN_EXPLORER|OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
 if(!GetSaveFileNameW(&dialog))return;
 try{loader::Export(source?ID_SOURCE_BUNDLE:ID_PAYLOAD,path,true);MessageBoxW(app.window,L"Extracted successfully.",L"nikogram",MB_OK|MB_ICONINFORMATION);}
 catch(...){MessageBoxW(app.window,L"Could not finish writing this file. Check the destination and free disk space. A partial file may remain.",L"Extraction failed",MB_OK|MB_ICONERROR);}
}
void ShowNotices(){
 auto data=loader::Resource(ID_NOTICES);std::string ascii(reinterpret_cast<const char*>(data.data()),data.size());
 int size=MultiByteToWideChar(CP_UTF8,0,ascii.data(),int(ascii.size()),nullptr,0);std::wstring text(size,L' ');
 MultiByteToWideChar(CP_UTF8,0,ascii.data(),int(ascii.size()),text.data(),size);MessageBoxW(app.window,text.c_str(),L"Nikogram third-party notices",MB_OK);
}
LRESULT CALLBACK Procedure(HWND w,UINT message,WPARAM a,LPARAM b){
 switch(message){
 case WM_CREATE:{
  app.window=w;app.scale=GetDpiForWindow(w)/96.f;app.brush=CreateSolidBrush(RGB(23,18,29));
  auto button=[&](int id,LPCWSTR title,DWORD style){return CreateWindowExW(0,L"BUTTON",title,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,0,0,0,0,w,reinterpret_cast<HMENU>(INT_PTR(id)),app.instance,nullptr);};
  app.inject=button(Inject,L"inject",BS_OWNERDRAW);app.keep=button(Keep,L"keep loader open",BS_AUTOCHECKBOX);SetWindowTheme(app.keep,L"",L"");
  app.dll=button(ExtractDll,L"extract dll",BS_OWNERDRAW);app.source=button(ExtractSource,L"extract source",BS_OWNERDRAW);
  app.minimize=button(Minimize,L"-",BS_OWNERDRAW);app.close=button(Close,L"x",BS_OWNERDRAW);app.notices=button(Notices,L"licenses",BS_OWNERDRAW);
  app.menuLabel=CreateWindowExW(0,L"STATIC",L"Startup menu",WS_CHILD|WS_VISIBLE,0,0,0,0,w,nullptr,app.instance,nullptr);
  app.menu=CreateWindowExW(0,L"COMBOBOX",L"Startup menu",WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,0,0,w,reinterpret_cast<HMENU>(INT_PTR(StartupMenu)),app.instance,nullptr);
  for(auto label:{L"Use saved preference",L"Nullcore",L"Moonlit"})SendMessageW(app.menu,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
  SendMessageW(app.menu,CB_SETCURSEL,0,0);
  SizeControls();Refresh();SetTimer(w,1,1000,nullptr);return 0;
 }
 case WM_DRAWITEM:{
  auto item=reinterpret_cast<DRAWITEMSTRUCT*>(b);Graphics g(item->hDC);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
  g.TranslateTransform(float(item->rcItem.left),float(item->rcItem.top));g.ScaleTransform(app.scale,app.scale);
  RectF rect(0,0,(item->rcItem.right-item->rcItem.left)/app.scale,(item->rcItem.bottom-item->rcItem.top)/app.scale);
  wchar_t title[128];GetWindowTextW(item->hwndItem,title,128);ButtonPaint(g,rect,int(item->CtlID),title,!(item->itemState&ODS_DISABLED),item->itemState&ODS_SELECTED,item->itemState&ODS_FOCUS);
  return TRUE;
 }
 case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN:{auto dc=reinterpret_cast<HDC>(a);SetTextColor(dc,RGB(185,167,200));SetBkColor(dc,RGB(23,18,29));return reinterpret_cast<LRESULT>(app.brush);}
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT paint{};HDC dc=BeginPaint(w,&paint);RECT r{};GetClientRect(w,&r);Bitmap frame(r.right,r.bottom);Graphics buffer(&frame);buffer.ScaleTransform(app.scale,app.scale);Paint(buffer);Graphics screen(dc);screen.DrawImage(&frame,0,0);EndPaint(w,&paint);return 0;}
 case WM_NCHITTEST:{POINT p{GET_X_LPARAM(b),GET_Y_LPARAM(b)};ScreenToClient(w,&p);if(p.y<app.Px(33)&&p.x<app.Px(670))return HTCAPTION;break;}
 case WM_DPICHANGED:{app.scale=HIWORD(a)/96.f;auto rect=reinterpret_cast<RECT*>(b);SetWindowPos(w,nullptr,rect->left,rect->top,app.Px(Width),app.Px(Height),SWP_NOZORDER|SWP_NOACTIVATE);SizeControls();return 0;}
 case WM_TIMER:if(a==1)Refresh();else if(a==2){KillTimer(w,2);if(SendMessageW(app.keep,BM_GETCHECK,0,0)!=BST_CHECKED)SendMessageW(w,WM_CLOSE,0,0);}return 0;
 case WM_COMMAND:switch(LOWORD(a)){
  case Minimize:ShowWindow(w,SW_MINIMIZE);return 0;
  case Close:SendMessageW(w,WM_CLOSE,0,0);return 0;
  case ExtractDll:if(!app.busy)Extract(false);return 0;
  case ExtractSource:if(!app.busy)Extract(true);return 0;
  case Notices:ShowNotices();return 0;
  case Inject:{
   if(app.busy||app.loaded)return 0;Refresh();if(!app.target.ready)return 0;
   bool confirmLegacy=app.target.legacyAttempt;
   if(confirmLegacy&&MessageBoxW(w,L"The old loader recorded an attempt without recording its mode. Nikogram is not in TF2's native module list.\n\nOnly continue if that attempt used NATIVE INJECT and Nikogram has fully unloaded. If you used manual mapping, or the outcome was uncertain, choose No and restart TF2.\n\nWas the previous attempt native, and has Nikogram fully unloaded?",L"Confirm previous native unload",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES)return 0;
   app.busy=true;EnableWindow(app.inject,FALSE);EnableWindow(app.dll,FALSE);EnableWindow(app.source,FALSE);EnableWindow(app.close,FALSE);InvalidateRect(w,nullptr,FALSE);
   auto target=app.target;const int startup=int(SendMessageW(app.menu,CB_GETCURSEL,0,0))-1;EnableWindow(app.menu,FALSE);
   std::thread([w,target,confirmLegacy,startup]{auto result=new loader::Result(loader::Inject(target,confirmLegacy,startup));if(!PostMessageW(w,ResultMessage,0,reinterpret_cast<LPARAM>(result)))delete result;}).detach();return 0;
  }
 }break;
 case ResultMessage:{
  std::unique_ptr<loader::Result> result(reinterpret_cast<loader::Result*>(b));app.busy=false;app.loaded=result->success;
  EnableWindow(app.dll,TRUE);EnableWindow(app.source,TRUE);EnableWindow(app.close,TRUE);InvalidateRect(w,nullptr,FALSE);
  EnableWindow(app.menu,TRUE);
  if(result->startupWarning)MessageBoxW(w,result->message.c_str(),L"Startup menu choice",MB_OK|MB_ICONWARNING);
  if(!result->success){MessageBoxW(w,result->message.c_str(),L"Nikogram was not loaded",MB_OK|MB_ICONERROR);Refresh();}
  else if(SendMessageW(app.keep,BM_GETCHECK,0,0)!=BST_CHECKED)SetTimer(w,2,1400,nullptr);
  return 0;
 }
 case WM_CLOSE:if(app.busy){MessageBoxW(w,L"Loading is still in progress. Please wait; closing now could leave an uncertain result.",L"nikogram",MB_OK);return 0;}DestroyWindow(w);return 0;
 case WM_DESTROY:KillTimer(w,1);PostQuitMessage(0);return 0;
 }
 return DefWindowProcW(w,message,a,b);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
 app.instance=instance;ULONG_PTR graphicsToken=0;GdiplusStartupInput input;
 if(GdiplusStartup(&graphicsToken,&input,nullptr)!=Ok)return 1;
 CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
 int result=0;
 try{
  if(!loader::VerifyPayloads())throw std::runtime_error("embedded payload mismatch");
  LoadLogo();int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
  if(count!=1){LocalFree(args);throw std::runtime_error("unknown arguments");}LocalFree(args);
  HANDLE singleton=CreateMutexW(nullptr,FALSE,L"Local\\NikogramOfflineLoader-0.1");
  if(!singleton||GetLastError()==ERROR_ALREADY_EXISTS){if(singleton)CloseHandle(singleton);MessageBoxW(nullptr,L"Nikogram loader is already open.",L"nikogram",MB_OK);return 0;}
  INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
  WNDCLASSEXW type{sizeof(type)};type.hInstance=instance;type.lpfnWndProc=Procedure;type.lpszClassName=L"NikogramOfflineLoader";type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hIcon=LoadIconW(nullptr,IDI_APPLICATION);RegisterClassExW(&type);
  UINT dpi=GetDpiForSystem();float scale=dpi/96.f;
  auto window=CreateWindowExW(WS_EX_APPWINDOW,type.lpszClassName,L"nikogram",WS_POPUP|WS_MINIMIZEBOX|WS_SYSMENU|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,int(Width*scale),int(Height*scale),nullptr,nullptr,instance,nullptr);
  if(!window)throw std::runtime_error("window creation");ShowWindow(window,show);UpdateWindow(window);
  MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
  CloseHandle(singleton);
 }catch(...){MessageBoxW(nullptr,L"The loader could not start or complete the requested file operation. Check the package and destination permissions.",L"nikogram",MB_OK|MB_ICONERROR);result=1;}
 app.logo.reset();if(app.logoStream){app.logoStream->Release();app.logoStream=nullptr;}CoUninitialize();GdiplusShutdown(graphicsToken);return result;
}
