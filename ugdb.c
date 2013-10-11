#define _XOPEN_SOURCE 500
#define _ISOC99_SOURCE  1
#define _POSIX_SOURCE  1

#include <features.h>
#include <stdio.h>   
#include <unistd.h>
#include <string.h>
#include <curses.h>   
#include <stdlib.h>
#include <math.h>
#include <sys/types.h>
#include <regex.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define IPC_KEY 8989

typedef struct oData
{
	char* ptr;
	struct oData* nex;
	struct oData* prv;
} oData;

typedef struct codeLine
{
	char* text;
	int linenum;
} codeLine;

typedef struct segCode
{
	char addr[20];
	char lab[100];
	char text[200];
}segCode;
typedef struct segLine{
	char startAdd[20];
	char endAdd[20];
	char name[100];
	char** segCode;
} segLine;

const char codeTitle[]="Code";
const char regTitle[]="Registers";
const char statusTitle[]="Status:";
const char statusTextP[]="[Opened File:%s][Entry Point:%s][TimeStamp:%d]";

int codeSX,codeSY,codeW,codeH;
int regSX,regSY,regW,regH;
int statusSY,statusSX;
FILE* outstream;
FILE* instream;
long lastOffset=0;
oData* codeOfFile=NULL;
char cmd[100];
int err;
char errbuf[1024];

int SW=0x00000000;
int FLD=0x00000001;

codeLine** codeDisBuf=NULL;
int codeDisBufLineNum=0;
int codeDisCurPoint=0;
int codeDisSLine=0;

char entryPointAddress[65];
char openedFilename[65];
char nofileisopenedText[]="no file is opened";

int seglinesnum=1024;
int seglineslen=0;
segLine seglines[1024];

int* mainThreadRunning;

void openFile();
void refreshCodeZone();
void clearCodeZone();
void createCodeLine(codeLine** cl,int len);
void freeCodeLine(codeLine** cl);

key_t ftok_key;
int shm_id;

void createRunningFlag(){
	ftok_key=ftok(".",IPC_KEY);
	shm_id=shmget(ftok_key,sizeof(int),IPC_CREAT|0x0644);
	if(shm_id==-1){
		printf("%d\n",shm_id);
	}
}
void setRunningFlag(int v){
	char* p=shmat(shm_id,0,0);
	*p=v;
	shmdt(p);
}

int getRunningFlag(){
	char* p=shmat(shm_id,0,0);
	int ret=*p;
	shmdt(p);
	return ret;
}
int rmRunningFlag(){
	shmctl(shm_id,IPC_RMID,NULL);
}

int minV(int v1,int v2){
	if(v1<v2) return v1;
	else return v2;
}
int getDigit(int n){
	int len=1;
	while(1){
		if(n/(int)pow(10,len)==0)break;	
		len++;
	}
	return len;
}
void covTabToSpace(char* p){
	while(1){
		if(*p=='\t' || *p=='\n')*p=' ';
		else if(*p=='\0')break;
		p++;
	}
}
void addToDisplayBuf(char* line){
	if(codeDisBuf==NULL){
		codeDisBuf=malloc(1024*sizeof(codeLine*));
		memset(codeDisBuf,0,1024*sizeof(codeLine*));
	}
	int len=strlen(line);
	createCodeLine(&codeDisBuf[codeDisBufLineNum],len+1);
	strcpy(codeDisBuf[codeDisBufLineNum]->text,line);
	codeDisBuf[codeDisBufLineNum]->linenum=codeDisBufLineNum;
	codeDisBufLineNum++;
}
void clearCodeDisBuf(){
	for(int i=0;i<codeDisBufLineNum;i++){
		freeCodeLine(&codeDisBuf[i]);
	}
	free(codeDisBuf);
	codeDisBuf=NULL;
	codeDisBufLineNum=0;
	codeDisCurPoint=0;
	codeDisSLine=0;
}

void refreshStatusText(){
	if(!(SW&FLD)){
		memcpy(openedFilename,nofileisopenedText,sizeof(nofileisopenedText));
	}
	mvprintw(statusSX,statusSY,statusTextP,openedFilename,entryPointAddress,time(NULL));
	refresh();
}

void refreshCodeZone(){
	// mvprintw(0,0,"Total:%d,Start:%d,Current:%d,Height:%d",codeDisBufLineNum,codeDisSLine,codeDisCurPoint,codeH);
	clearCodeZone();
	int i=0;
	int cl=i+codeDisSLine;
	int lineHeadLen=getDigit(minV(codeDisSLine+codeH<codeDisBufLineNum?(codeDisSLine+codeH-1):(codeDisSLine+codeH),codeDisBufLineNum))+1;
	char lineP[10];
	memset(lineP,0,sizeof(lineP));
	sprintf(lineP,"%%-%d.%ds",codeW-lineHeadLen,codeW-lineHeadLen);
	for(;i<codeH;i++){
		cl=i+codeDisSLine;
		if(cl>=codeDisBufLineNum)break;
		if(cl==codeDisCurPoint)attron(A_REVERSE);
		if((codeDisSLine>0 && i==0)||(codeDisSLine+codeH<codeDisBufLineNum && i==codeH-1)){
			mvprintw(codeSX+i,codeSY+lineHeadLen,"...");
		}else{
			mvprintw(codeSX+i,codeSY,"%d:",cl+1);
			char tmpline[1024];
			memset(tmpline,0,sizeof(tmpline));
			sprintf(tmpline,lineP,codeDisBuf[cl]->text);
			covTabToSpace(tmpline);
			mvprintw(codeSX+i,codeSY+lineHeadLen,"%s",tmpline);
		}
		if(cl==codeDisCurPoint)attroff(A_REVERSE);
		// if(cl==codeDisCurPoint)mvprintw(0,0,"%70.70s"," ");
		// if(cl==codeDisCurPoint)mvprintw(0,0,"%s,%s,%s",seglines[cl].startAdd,seglines[cl].endAdd,seglines[cl].name);
	}

}

void moveUp(){
	(codeDisCurPoint<=0)?(codeDisCurPoint==0):(codeDisCurPoint--);
	if(codeDisSLine>=codeDisCurPoint)codeDisSLine--;
	codeDisSLine<0?codeDisSLine=0:1;
	refreshCodeZone();
}
void moveDown(){
	(codeDisCurPoint>=codeDisBufLineNum-1)?(codeDisCurPoint==codeDisBufLineNum-1):(codeDisCurPoint++);
	if(codeDisSLine+codeH-1<=codeDisCurPoint)codeDisSLine=codeDisCurPoint-codeH+2;
	refreshCodeZone();
}

void readOutputSleep(){
	usleep((1000000/1000)*2);
}

void createOData(oData** data, int dataLen){
	*data=(oData*)malloc(sizeof(oData));
	(*data)->ptr=(char*)malloc(dataLen);
	memset((*data)->ptr,'\0',dataLen);
	(*data)->nex=NULL;
	(*data)->prv=NULL;
}

void freeOData(oData** data){
	if(*data!=NULL){
		if((*data)->ptr!=NULL){
			free((*data)->ptr);
		}
		free(*data);
		*data=NULL;
	}
}

void createCodeLine(codeLine** cl,int len){
	*cl=(codeLine*)malloc(sizeof(struct codeLine));
	(*cl)->text=malloc(len);
}

void freeCodeLine(codeLine** cl){
	if(*cl!=NULL){
		if((*cl)->text!=NULL){
			free((*cl)->text);
		}
		free(*cl);
		*cl=NULL;
	}
}

void initial()   
{   
	initscr();   
	cbreak();   
	nonl();   
	noecho();
	// raw();
	intrflush(stdscr,FALSE);
	keypad(stdscr,TRUE);   
	refresh();   
}

void clearZone(int startX,int startY,int lines,int clos){
	char _blank[1024];
	memset(_blank,' ',sizeof(_blank));
	_blank[clos]='\0';
	for(int i=startX;i<startX+lines;i++){
		mvprintw(i,startY,"%s",_blank);
	}
}

void clearCodeZone(){
	clearZone(codeSX,codeSY,codeH,codeW);
}

void clearAllZone(){
	clearZone(0,0,LINES,COLS);
}

void drawFrame(){
	codeSY=1;codeSX=3;codeW=80;codeH=LINES-6;
	regSY=codeW+2;regSX=3;regW=COLS-codeW-3;regH=LINES-6;
	statusSY=2+strlen(statusTitle);statusSX=LINES-2;

	clearAllZone();

	// box(stdscr,ACS_VLINE,ACS_HLINE);
	border(ACS_VLINE, ACS_VLINE, ACS_HLINE, ACS_HLINE, 0, 0, 0, 0);
	mvhline(2,1,ACS_HLINE,COLS-2);
	mvhline(LINES-3,1,ACS_HLINE,COLS-2);
	mvvline(1,codeW+2,ACS_VLINE,LINES-4);

	mvaddch(2,codeW+2,ACS_PLUS);
	mvaddch(LINES-3,codeW+2,ACS_BTEE);
	mvaddch(0,codeW+2,ACS_TTEE);
	mvaddch(2,COLS-1,ACS_RTEE);
	mvaddch(LINES-3,COLS-1,ACS_RTEE);
	mvaddch(2,0,ACS_LTEE);
	mvaddch(LINES-3,0,ACS_LTEE);
	
	mvaddstr(1,codeSY+(codeW-strlen(codeTitle))/2,codeTitle);
	mvaddstr(1,regSY+(regW-strlen(regTitle))/2,regTitle);
	mvaddstr(LINES-2,1,statusTitle);

	refresh();
}


int openInputWin(char* message,char* input){
	int w=100,h=3;
	WINDOW* win=newwin(h,w,(LINES-h)/2,(COLS-w)/2);
	box(win,ACS_VLINE,ACS_HLINE);
	touchwin(win);
	mvwprintw(win,1,2,"%s:",message);
	wrefresh(win);
	refresh();
	mvwscanw(win,2+strlen(message),2,"%s",input);
	delwin(win);
	// touchwin(stdscr);
	// refresh();
	return 0;
}

void end(){
	nocbreak();
	echo();
	nl();
	endwin();
	freeOData(&codeOfFile);
}

void clearOutputBuf(){
	readOutputSleep();
	instream=fopen("gdb.tmp","r");
	fseek(instream,lastOffset,SEEK_SET);
	char ch;
	while((ch=fgetc(instream))!=EOF);
	lastOffset=ftell(instream);
	fclose(instream);	
}

void initGdb(){
	outstream=popen("rm -rf gdb.tmp;gdb&>gdb.tmp","w");
	readOutputSleep();
	clearOutputBuf();
}
void endGdb(){
	pclose(outstream);
}


void readOutput(oData** ret){
	readOutputSleep();
	instream=fopen("gdb.tmp","r");
	fseek(instream,lastOffset,SEEK_SET);

	oData* lastData=NULL;
	while(1){
		oData* data;
		createOData(&data,1024);
		fscanf(instream,"%1023c",data->ptr);
		if(lastData!=NULL){
			lastData->nex=data;
			data->prv=lastData;
		}
		lastData=data;
		if(strlen(lastData->ptr)<1023){
			break;
		}
	}

	if(lastData->nex==NULL && lastData->prv==NULL){
		*ret=lastData;
	}else{
		while(lastData->nex!=NULL){
			lastData=lastData->nex;
		}
		int c=1;
		while(lastData->prv!=NULL){
			lastData=lastData->prv;
			c++;
		}
		createOData(ret,1024*c);
		char* p=(*ret)->ptr;
		while(lastData!=NULL){
			strcpy(p,lastData->ptr);
			p+=1023;

			if(lastData->nex!=NULL){
				lastData=lastData->nex;
				freeOData(&(lastData->prv));
			}else{
				freeOData(&lastData);
			}
		}
	}

	lastOffset=ftell(instream);
	fclose(instream);
}

void execCmd(char* cmd){
	fprintf(outstream,"%s\n",cmd);
	fflush(outstream);
}

int parseEntryPoint(char* line,char* target,int targetLen){
	int ret=0;
	regex_t compiled;
	err=regcomp(&compiled,"Entry point: ([0-9a-fxA-F]*)",REG_EXTENDED);
	if(err==0){
		int matchLen=2;
		regmatch_t match[matchLen];
		err=regexec(&compiled,line,matchLen,match,0);
		if(err==0){
			int len=match[1].rm_eo-match[1].rm_so;
			if(len){
				memset(target,'\0',targetLen);
				memcpy(target,line+match[1].rm_so,len);
				ret=1;
			}
		}
	}
	if(err){
		regerror(err,&compiled,errbuf,sizeof(errbuf));
		ret=-1;
	}
	regfree(&compiled);
	return ret;
}

void freeSegLine(){
	seglineslen=0;
}

int parseSegLine(char* line){
	int ret=0;
	regex_t compiled;
	err=regcomp(&compiled,"(0x[0-9a-fA-F]*)*\\s*-\\s*(0x[0-9a-fA-F]*)*\\s*is\\s*(\\.[^\\s]*)\\s*$",REG_EXTENDED);
	if(err==0){
		int matchLen=4;
		regmatch_t match[matchLen];
		err=regexec(&compiled,line,matchLen,match,0);
		if(err==0){
			memset(seglines[seglineslen].startAdd,'\0',sizeof(seglines[seglineslen].startAdd));
			memset(seglines[seglineslen].endAdd,'\0',sizeof(seglines[seglineslen].endAdd));
			memset(seglines[seglineslen].name,'\0',sizeof(seglines[seglineslen].name));
			int len=match[1].rm_eo-match[1].rm_so;
			if(len){
				memcpy(seglines[seglineslen].startAdd,line+match[1].rm_so,len);
			}
			len=match[2].rm_eo-match[2].rm_so;
			if(len){
				memcpy(seglines[seglineslen].endAdd,line+match[2].rm_so,len);
			}
			len=match[3].rm_eo-match[3].rm_so;
			if(len){
				memcpy(seglines[seglineslen].name,line+match[3].rm_so,len);
			}
			seglineslen++;
			ret=1;
		}
	}
	if(err){
		regerror(err,&compiled,errbuf,sizeof(errbuf));
		ret=-1;
	}
	regfree(&compiled);
	return ret;
}

void parseSegInfoLine(char* line){
	if(strlen(entryPointAddress)==0){
		parseEntryPoint(line,entryPointAddress,sizeof(entryPointAddress));
	}
	if(parseSegLine(line)==1){
		addToDisplayBuf(line);
	}
}

void loadSegInfo(){
	if(SW & FLD >0){
		execCmd("i files");

		freeOData(&codeOfFile);
		readOutput(&codeOfFile);

		int lines=0;
		char* p=codeOfFile->ptr;
		while(*p!='\0'){
			if(*p=='\n')
				lines++;
			p++;
		}
		lines++;

		clearCodeDisBuf();
		p=codeOfFile->ptr;
		char* tmpLine;
		tmpLine=p;
		int i=0;
		while(*p!='\0'){
			if(*p=='\n'){
				i++;
				*p='\0';
				parseSegInfoLine(tmpLine);
				tmpLine=p+1;
			}
			p++;
		}
		parseSegInfoLine(tmpLine);

		freeOData(&codeOfFile);

		refreshCodeZone();

	}
}

void openFile(){
	execCmd("file a.out");
	clearOutputBuf();
	SW=SW|FLD;

	char filename[]="a.out";
	strcpy(openedFilename,filename);
	// refreshStatusText();
}

int main(int argc,char *argv[]){
	// createRunningFlag();
	// setRunningFlag(1);

	initGdb();
	initial();
	drawFrame();
	curs_set(0);
	timeout(300);
	int ch;
	while((ch=getch())!=27){
		switch(ch){
		case 'o':
			openFile();
			break;
		case 'l':
			loadSegInfo();
			break;
		case 'c':
			clearCodeZone();
			break;
		case 'r':
			drawFrame();
			break;
		case KEY_UP:
			moveUp();
			break;
		case KEY_DOWN:
			moveDown();
			break;
		default:
			;
		}
		refreshStatusText();
	}
	// setRunningFlag(0);
	end();

	return 0;
}


