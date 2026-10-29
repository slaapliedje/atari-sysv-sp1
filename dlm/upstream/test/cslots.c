#include "../dlm_slot.c"
#include <limits.h>

struct user u;
struct execsw __amix_execsw[3];
struct hooksw hooksw[] = { { 0 } };
struct cdevsw cdevsw[70];
int cdevcnt = 70, nexectype;
unsigned long host_klo = 0x1000, host_khi = 0x2000;
static struct dlm_mod modules[70];
static unsigned char images[70][256];
static int checks, allocs, frees, fail_alloc, race_major = -1, used_major = -1;
static char *race_name;
static int calls, open_error;

#define C(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL line%d check%d\n",__LINE__,checks); exit(1); } } while(0)
int nodev() { return ENXIO; }
static int used() { return 0; }
char *dlm_zalloc(n) long n;
{
 if(fail_alloc) return 0;
 if(race_major>=0) { int mj=race_major; race_major=-1; C(dlm_creg(race_name,mj)==0); }
 if(used_major>=0) { cdevsw[used_major].d_open=used; used_major=-1; }
 allocs++; return calloc(1,n);
}
void dlm_free(p,n) char*p; long n; { frees++; free(p); }
char *host_rp(m,a) struct dlm_mod*m; unsigned long a; { return m->m_base+a-m->m_run; }
int host_call(m,f,a,b) struct dlm_mod*m; unsigned long f; char*a; long b;
{ calls++; return f==m->m_run+192 ? open_error : 0; }
void dlm_hold(m) struct dlm_mod*m; { m->m_refs++; }
void dlm_rele(m) struct dlm_mod*m; { C(m->m_refs>0); m->m_refs--; }
void dlm_abort(g) struct dlm_guard*g; { }
static void word(p,n) unsigned char*p; unsigned long n;
{ p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }
static struct dlm_mod *image(name,mj,n)
char *name; int mj,n;
{
 struct dlm_mod*m=&modules[mj]; unsigned char*b=images[mj];
 memset(m,0,sizeof*m); memset(b,0,256); strcpy(m->m_name,name);
 m->m_base=(char*)b; m->m_run=0x100000+4096*mj; m->m_size=256;
 word(b+4,m->m_run+32); word(b+32+DRV_CMAJOR,mj); word(b+32+DRV_CCOUNT,n);
 word(b+32+DRV_CDEVSW,m->m_run+192); word(b+32+DRV_CDEVSW+4,m->m_run+194);
 return m;
}
int dlm_load(name,flags,root,g,mp)
char*name; int flags,root; struct dlm_guard*g; struct dlm_mod**mp;
{
 struct dlm_cslot*cs; struct dlm_mod*m; int n;
 for(cs=dlm_cslot;cs;cs=cs->cs_next) if(!strcmp(cs->cs_name,name)) break;
 if(!cs) return ENOENT;
 n=!strcmp(name,"wide")?12:1;
 m=image(name,!strcmp(name,"wide")?40:cs->cs_major,n);
 *mp=m; return drv_install(m,m->m_run);
}
static int unload(m) struct dlm_mod*m;
{ return m->m_refs || m->m_incall ? EBUSY : drv_remove(m,0); }
static int count() { struct dlm_cslot*cs; int n=0; for(cs=dlm_cslot;cs;cs=cs->cs_next)n++; return n; }
int main()
{
 int i,j,before; char name[32]; dev_t dev; struct dlm_mod*m; struct modspecific_stat st;
 for(i=0;i<cdevcnt;i++) for(j=0;j<CD_NFN;j++) cfn(&cdevsw[i])[j]=nodev;
 C(!dlm_cslot && !allocs);
 cdevsw[3].d_open=used;
 C(dlm_creg("busy",3)==EEXIST && !allocs);
 C(dlm_creg("bad",-1)==ECONFIG && dlm_creg("bad",70)==ECONFIG && !allocs);
 fail_alloc=1; C(dlm_creg("nomem",2)==ENOMEM && !cslot(2) && cempty(&cdevsw[2])); fail_alloc=0;
 for(i=20;i<30;i++) { sprintf(name,"driver%d",i); C(dlm_creg(name,i)==0); }
 C(count()==10 && allocs==10 && cslot(29));
 before=allocs; C(dlm_creg("driver29",29)==0 && allocs==before);
 C(dlm_creg("different",29)==EEXIST && allocs==before);
 for(i=40;i<52;i++) C(dlm_creg("wide",i)==0);
 m=image("wide",40,12); C(drv_install(m,m->m_run)==0);
 drv_info(m,0,&st); C(st.mss_p1[0]==40 && st.mss_p1[1]==12);
 C(drv_remove(m,0)==0 && cslot(51) && !cslot(51)->cs_mod);
 C(cdevsw[51].d_open==dlm_cdev_open && cdevsw[51].d_read==nodev);
 m=image("bounds",69,INT_MAX); C(drv_install(m,m->m_run)==EINVAL);
 word(images[69]+32+DRV_CMAJOR,0x7fffffff); word(images[69]+32+DRV_CCOUNT,2); C(drv_install(m,m->m_run)==EINVAL);
 word(images[69]+32+DRV_CMAJOR,0xffffffff); C(drv_install(m,m->m_run)==EINVAL);
 race_major=5; race_name="race"; before=allocs-frees; C(dlm_creg("race",5)==0 && allocs-frees==before+1 && count()==23);
 race_major=6; race_name="winner"; before=allocs-frees; C(dlm_creg("loser",6)==EEXIST && !strcmp(cslot(6)->cs_name,"winner") && allocs-frees==before+1);
 used_major=7; before=allocs-frees; C(dlm_creg("taken",7)==EEXIST && !cslot(7) && allocs-frees==before);
 dev=makedevice(29,0); C(dlm_cdev_open(&dev,0,OTYP_CHR,0)==0); m=cslot(29)->cs_mod;
 C(m && m->m_refs==1 && cslot(29)->cs_nopen==1 && calls==1);
 C(unload(m)==EBUSY && cslot(29)->cs_mod==m);
 C(dlm_cdev_open(&dev,0,OTYP_CHR,0)==0 && m->m_refs==1);
 C(dlm_cdev_close(dev,0,OTYP_CHR,0)==0 && !m->m_refs);
 C(unload(m)==0 && !cslot(29)->cs_mod && cdevsw[29].d_open==dlm_cdev_open);
 C(dlm_cdev_open(&dev,0,OTYP_CHR,0)==0 && cslot(29)->cs_mod==m && m->m_refs==1);
 C(dlm_cdev_close(dev,0,OTYP_CHR,0)==0 && unload(m)==0);
 open_error=ENXIO; C(dlm_cdev_open(&dev,0,OTYP_CHR,0)==ENXIO && !m->m_refs); open_error=0;
 C(unload(m)==0);
 for(i=0;i<cdevcnt;i++) if(cempty(&cdevsw[i])) { sprintf(name,"rest%d",i); C(dlm_creg(name,i)==0); }
 C(count()==cdevcnt-2 && allocs-frees==count());
 C(dlm_creg("rest69",69)==0 && count()==cdevcnt-2);
 while(dlm_cslot) { struct dlm_cslot*cs=dlm_cslot; dlm_cslot=cs->cs_next; dlm_free((char*)cs,sizeof*cs); }
 C(allocs==frees);
 printf("PASS %d character-slot production checks\n",checks); return 0;
}
