// Production importer, VFS browser and Exchange calls against controlled Palm APIs.
#include <PalmOS.h>
#include <VFSMgr.h>
#include <assert.h>
#include "../src/mapio.h"
#include "../src/resource.h"
struct Handle { UInt8 *p; UInt32 n; };
struct Database { Char name[32]; UInt32 type, creator; struct Handle rec[300]; UInt16 n; Boolean live; } dbs[12];
struct Form { int unused; } form;
struct List { Int16 sel; UInt16 n; } list;
static const UInt8 minimal[] = "BMAPBOLO\1\0\0\0\4\377\377\377";
static UInt8 data[1048578], sentData[1048578];
static UInt32 len, pos, shortRead, shortSend, sentLen;
static int failWrite, failRename, failOpen, failRecord, failReadAt, canceled, disconnects, accepts, closed, alerts;
static Err disconnected;
static int actions[20], actionAt, dirEntries, volumes, activeVolume, cardGone, noVfs;
static int failMapOpen;
static int cancelEvent, requeued, zeroSend, dirFolder;
static Char currentPath[512];
static struct Handle builtin;
static void reset(void) {
 unsigned i,j;
 for(i=0;i<12;i++) for(j=0;j<dbs[i].n;j++) free(dbs[i].rec[j].p);
 memset(dbs,0,sizeof(dbs));
 memcpy(data,minimal,16); len=16;pos=0;shortRead=0;shortSend=0;sentLen=0;
 failWrite=failRename=failOpen=failRecord=canceled=disconnects=accepts=closed=alerts=0;
 failReadAt=-1;disconnected=0;actionAt=0;dirEntries=1;volumes=1;activeVolume=1;cardGone=noVfs=0;
 memset(actions,0,sizeof(actions)); actions[0]=MapCardOpen;
 builtin.p=data;builtin.n=len;cancelEvent=requeued=zeroSend=dirFolder=0;currentPath[0]=0;
}
UInt16 DmNumRecords(DmOpenRef d){return d->n;}
MemHandle DmQueryRecord(DmOpenRef d,UInt16 r){return r<d->n?&d->rec[r]:NULL;}
UInt32 MemHandleSize(MemHandle h){return h->n;}
void *MemHandleLock(MemHandle h){return h->p;}
Err MemHandleUnlock(MemHandle h){(void)h;return 0;}
LocalID DmFindDatabase(UInt16 c,const Char *n){int i;(void)c;for(i=0;i<12;i++)if(dbs[i].live&&!strcmp(dbs[i].name,n))return i+1;return 0;}
Err DmCreateDatabase(UInt16 c,const Char *n,UInt32 cr,UInt32 t,Boolean r){int i;(void)c;(void)r;for(i=0;i<12;i++)if(!dbs[i].live){dbs[i].live=1;strcpy(dbs[i].name,n);dbs[i].type=t;dbs[i].creator=cr;return 0;}return 2;}
DmOpenRef DmOpenDatabase(UInt16 c,LocalID id,UInt16 m){(void)c;(void)m;return failOpen?NULL:&dbs[id-1];}
MemHandle DmNewRecord(DmOpenRef d,UInt16 *r,UInt32 n){MemHandle h;if(failRecord)return NULL;assert(d->n<300);*r=d->n++;h=&d->rec[*r];h->n=n;h->p=malloc(n);return h;}
Err DmWrite(void *p,UInt32 off,const void *b,UInt32 n){if(failWrite)return 2;memcpy((UInt8*)p+off,b,n);return 0;}
Err DmReleaseRecord(DmOpenRef d,UInt16 r,Boolean dirty){(void)d;(void)r;(void)dirty;return 0;}
Err DmCloseDatabase(DmOpenRef d){(void)d;closed++;return 0;}
Err DmGetNextDatabaseByTypeCreator(Boolean n,DmSearchStateType *st,UInt32 t,UInt32 c,Boolean o,UInt16 *card,LocalID *id){int i;(void)n;(void)st;(void)o;for(i=0;i<12;i++)if(dbs[i].live&&dbs[i].type==t&&dbs[i].creator==c){*card=0;*id=i+1;return 0;}return 1;}
Err DmDeleteDatabase(UInt16 c,LocalID id){unsigned i;(void)c;for(i=0;i<dbs[id-1].n;i++)free(dbs[id-1].rec[i].p);memset(&dbs[id-1],0,sizeof(dbs[0]));return 0;}
Err DmSetDatabaseInfo(UInt16 c,LocalID id,const Char *name,UInt16 *a,void *b,void *d,void *e,void *f,void *g,void *h,void *i,UInt32 *t,void *j){(void)c;(void)a;(void)b;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;if(failRename)return 2;strcpy(dbs[id-1].name,name);dbs[id-1].type=*t;return 0;}
static UInt32 readData(void *ctx,UInt8 *p,UInt32 max,Err *err){UInt32 n;(void)ctx;*err=0;if(canceled){*err=exgErrUserCancel;return 0;}if(failReadAt>=0&&pos>=(unsigned)failReadAt){*err=exgErrDisconnected;return 0;}n=len-pos;if(n>max)n=max;if(shortRead&&n>shortRead)n=shortRead;memcpy(p,data+pos,n);pos+=n;return n;}
Err ExgAccept(ExgSocketType *s){(void)s;accepts++;return 0;}
Err ExgDisconnect(ExgSocketType *s,Err e){(void)s;disconnects++;disconnected=e;return 0;}
UInt32 ExgReceive(ExgSocketType *s,void *p,UInt32 max,Err *err){return readData(s,p,max,err);}
UInt32 ExgSend(ExgSocketType *s,const void *p,UInt32 n,Err *err){(void)s;*err=0;if(zeroSend)return 0;if(canceled){*err=exgErrUserCancel;return 0;}if(shortSend&&n>shortSend)n=shortSend;memcpy(sentData+sentLen,p,n);sentLen+=n;return n;}
Err ExgPut(ExgSocketType *s){assert(s->length==len);return canceled?exgErrUserCancel:0;}
Err ExgRegisterData(UInt32 c,UInt16 i,const Char *t){(void)c;(void)i;(void)t;return 0;}
Err FtrGet(UInt32 c,UInt16 n,UInt32 *v){(void)c;(void)n;*v=1;return noVfs?1:0;}
UInt16 FrmCustomAlert(UInt16 n,const Char *a,const Char *b,const Char *c){(void)n;(void)a;(void)b;(void)c;alerts++;return 0;}
FormType *FrmGetActiveForm(void){return &form;}
FormType *FrmInitForm(UInt16 n){(void)n;return &form;}
void FrmSetActiveForm(FormType *f){(void)f;}
void *FrmGetObjectPtr(FormType *f,UInt16 n){(void)f;(void)n;return &list;}
UInt16 FrmGetObjectIndex(FormType *f,UInt16 n){(void)f;return n;}
void LstSetListChoices(ListType *l,Char **s,UInt16 n){(void)s;l->n=n;}
void LstSetSelection(ListType *l,Int16 n){l->sel=n;}
static char lastLabel[32];
void FrmCopyLabel(FormType *f,UInt16 n,const Char *s){(void)f;(void)n;assert(strlen(s)<=24);strcpy(lastLabel,s);}
UInt16 FrmDoDialog(FormType *f){int a;(void)f;assert(actionAt<20);a=actions[actionAt++];list.sel=0;return a?a:MapCardCancel;}
Int16 LstGetSelection(ListType *l){return l->sel;}
void FrmDeleteForm(FormType *f){(void)f;}
Boolean MapFileSum(UInt16 i,const Char *n,UInt32 *s,UInt32 *l){(void)i;(void)n;*s=0;*l=len;return 1;}
Boolean MapBytesOpen(MapBytes *m,UInt16 i,const Char *n){memset(m,0,sizeof(*m));if(!i){builtin.n=len;m->res=&builtin;}else m->db=&dbs[DmFindDatabase(0,n)-1];return 1;}
void MapBytesClose(MapBytes *m){(void)m;}
Err VFSFileRead(FileRef f,UInt32 max,void *p,UInt32 *n){Err e;(void)f;if(cardGone)return 9;*n=readData(NULL,p,max,&e);if(e)return e;return *n?0:vfsErrFileEOF;}
Err VFSFileOpen(UInt16 v,const Char *path,UInt16 m,FileRef *f){(void)m;strcpy(currentPath,path);activeVolume=v;if(cardGone)return 9;if(failMapOpen&&strstr(path,".map")){failMapOpen=0;return 9;}*f=strstr(path,".map")?2:1;return 0;}
Err VFSFileClose(FileRef f){(void)f;return 0;}
Err VFSFileSize(FileRef f,UInt32 *n){(void)f;*n=len;return 0;}
Err VFSVolumeEnumerate(UInt16 *v,UInt32 *it){if((int)*it>=volumes)return 9;*v=*it+1;(*it)++;if((int)*it==volumes)*it=vfsIteratorStop;return 0;}
Err VFSDirEntryEnumerate(FileRef f,UInt32 *it,FileInfoType *info){(void)f;if((int)*it>=dirEntries){*it=vfsIteratorStop;return expErrEnumerationEmpty;}sprintf(info->nameP,"Map%lu.map",(unsigned long)*it);info->attributes=0;if(dirFolder&&(dirFolder==2||!strcmp(currentPath,"/"))&&!*it){strcpy(info->nameP,dirFolder==2?"Long folder name":"Folder");info->attributes=vfsFileAttrDirectory;}(*it)++;if((int)*it==dirEntries)*it=vfsIteratorStop;return 0;}
void EvtGetEvent(EventType *e,Int32 wait){(void)wait;memset(e,0,sizeof(*e));if(cancelEvent){e->eType=cancelEvent;e->data.ctlSelect.controlID=MapCopyCancel;cancelEvent=0;}}
void EvtAddEventToQueue(EventType *e){(void)e;requeued++;}
Boolean SysHandleEvent(EventType *e){(void)e;return 0;}
Boolean FrmHandleEvent(FormType *f,EventType *e){(void)f;(void)e;return 0;}
void FrmDrawForm(FormType *f){(void)f;}
static int count(void){int i,n=0;for(i=0;i<12;i++)if(dbs[i].live){assert(dbs[i].type!='Mimp'||dbs[i].creator!='Rmrf');if(dbs[i].type=='Map ')n++;}return n;}
static void importFail(void){Char name[32];assert(MapImport(readData,NULL,0,"bad.map",name)!=0);assert(count()==0);}
int main(void){Char name[32];ExgSocketType s;unsigned i;
 reset();assert(!MapImport(readData,NULL,len,"Everard Island.map",name));assert(!strcmp(name,"Everard Island 2"));assert(count()==1);
 pos=0;assert(!MapImport(readData,NULL,0,"Everard Island.map",name));assert(!strcmp(name,"Everard Island 3"));assert(count()==2);
 reset();assert(!DmCreateDatabase(0,"Save",0,'Save',0));strcpy(name,dbs[0].name);
 assert(!MapImport(readData,NULL,0,"Save.map",name));assert(!strcmp(dbs[0].name,"Save")&&dbs[0].type=='Save');assert(!strcmp(name,"Save 2"));assert(count()==1);
 reset();DmCreateDatabase(0,"PalmBoloMapCache",0,0,0);DmCreateDatabase(0,"PalmBoloSavedMap",0,0,0);DmCreateDatabase(0,"palmbolomapin",0,0,0);
 assert(!MapImport(readData,NULL,len,"PalmBoloMapCache.map",name));assert(!strcmp(name,"PalmBoloMapCache 2"));
 pos=0;assert(!MapImport(readData,NULL,len,"PalmBoloSavedGame.map",name));assert(!strcmp(name,"PalmBoloSavedGame 2"));
 pos=0;assert(!MapImport(readData,NULL,len,"PalmBolo import.map",name));assert(!strcmp(name,"PalmBolo import 2"));
 pos=0;assert(!MapImport(readData,NULL,len,"PalmBoloMapIn.map",name));assert(!strcmp(name,"PalmBoloMapIn 2"));
 reset();DmCreateDatabase(0,"PalmBolo import",'Rmrf','Mimp',0);DmCreateDatabase(0,"Other stage",'Xxxx','Mimp',0);DmCreateDatabase(0,"Keep",'Rmrf','Map ',0);
 assert(!MapImport(readData,NULL,len,"New.map",name));assert(!strcmp(name,"New"));assert(!DmFindDatabase(0,"PalmBolo import")&&DmFindDatabase(0,"Other stage")&&DmFindDatabase(0,"Keep")&&count()==2);
 reset();shortRead=1;len=70000;memset(data+16,42,len-16);assert(!MapImport(readData,NULL,len,"/folder/Big.map",name));assert(count()==1);assert(dbs[0].n==18);
 MapBeam(1,name);assert(sentLen==len&&!memcmp(data,sentData,len));
 reset();zeroSend=1;MapBeam(0,"Everard Island");assert(disconnected==exgErrDisconnected);
 reset();shortSend=3;MapBeam(0,"Everard Island");assert(sentLen==16&&!memcmp(data,sentData,16));assert(disconnects==1);
 for(i=0;i<12;i++){reset();len=i;importFail();}
 reset();data[0]='X';importFail();reset();data[8]=2;importFail();reset();data[9]=255;importFail();
 reset();data[12]=3;importFail();reset();len=15;importFail();
 reset();len=12;assert(!MapImport(readData,NULL,0,"No end.map",name));assert(count()==1);
 reset();len=1048577;importFail();reset();len=16;assert(MapImport(readData,NULL,17,"short.map",name));assert(count()==0);
 reset();assert(MapImport(readData,NULL,15,"long.map",name));assert(count()==0);
 reset();failWrite=1;importFail();reset();failRecord=1;importFail();reset();failOpen=1;importFail();reset();failRename=1;importFail();
 reset();shortRead=3;failReadAt=9;importFail();reset();canceled=1;importFail();
 reset();memset(&s,0,sizeof(s));s.name="Incoming.map";assert(!MapReceive(&s));assert(accepts==1&&disconnects==1&&!disconnected);assert(count()==1);
 reset();s.length=100;assert(!MapReceive(&s));assert(disconnects==1&&disconnected);assert(count()==0);
 reset();canceled=1;assert(!MapReceive(&s));assert(disconnects==1&&disconnected==exgErrUserCancel);assert(count()==0);
 reset();MapBrowse();assert(count()==1);assert(!strcmp(dbs[0].name,"Map0"));
 reset();actions[0]=MapCardCancel;MapBrowse();assert(count()==0);
 reset();noVfs=1;MapBrowse();assert(alerts==1&&count()==0);
 reset();cardGone=1;MapBrowse();assert(alerts==1&&count()==0);
 reset();shortRead=2;failReadAt=8;MapBrowse();assert(alerts==1&&count()==0);
 reset();volumes=2;actions[0]=MapCardVolume;actions[1]=MapCardOpen;MapBrowse();assert(activeVolume==2&&count()==1);
 reset();dirEntries=10;actions[0]=MapCardMore;actions[1]=MapCardOpen;MapBrowse();assert(!strcmp(dbs[0].name,"Map7")&&count()==1);
 reset();dirEntries=7;actions[0]=MapCardMore;actions[1]=MapCardOpen;MapBrowse();assert(!strcmp(dbs[0].name,"Map0"));
 reset();dirEntries=8;actions[0]=MapCardMore;actions[1]=MapCardOpen;MapBrowse();assert(!strcmp(dbs[0].name,"Map7"));
 reset();dirEntries=14;actions[0]=MapCardMore;actions[1]=MapCardMore;actions[2]=MapCardOpen;MapBrowse();assert(!strcmp(dbs[0].name,"Map0"));
 reset();dirEntries=10;actions[0]=MapCardMore;actions[1]=MapCardOpen;MapBrowse();assert(count()==1);
 reset();dirEntries=10;failMapOpen=1;actions[0]=MapCardMore;actions[1]=MapCardOpen;actions[2]=MapCardOpen;MapBrowse();assert(alerts==1&&count()==1&&!strcmp(dbs[0].name,"Map7"));
 reset();dirFolder=1;actions[0]=MapCardOpen;actions[1]=MapCardOpen;MapBrowse();assert(count()==1&&!strcmp(currentPath,"/Folder/Map0.map"));
 reset();dirFolder=1;actions[0]=MapCardOpen;actions[1]=MapCardUp;actions[2]=MapCardCancel;MapBrowse();assert(count()==0&&!strcmp(currentPath,"/"));
 reset();dirFolder=1;actions[0]=MapCardOpen;actions[1]=MapCardCancel;MapBrowse();assert(!strcmp(lastLabel,"Card 1: /Folder"));
 reset();dirFolder=2;actions[0]=MapCardOpen;actions[1]=MapCardOpen;actions[2]=MapCardCancel;MapBrowse();assert(strlen(lastLabel)==24&&!strncmp(lastLabel,"Card 1: \x18",9)&&!strcmp(lastLabel+9,"g folder name/Long folder name"+15));
 reset();cancelEvent=ctlSelectEvent;MapBrowse();assert(count()==0&&!alerts);
 reset();cancelEvent=appStopEvent;MapBrowse();assert(count()==0&&requeued==1);
 reset();puts("map-test: importer, storage rollback, VFS and Exchange adapters passed");return 0;
}
