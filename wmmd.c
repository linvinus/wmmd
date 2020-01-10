/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 * 
 */


#define _GNU_SOURCE


#include <linux/limits.h> /*PATH_MAX*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> //getpagesize
#include <stdlib.h>     /* qsort */
#include <fcntl.h> /*O_RDONLY*/
#include <ctype.h> /*isspace*/
#include <errno.h> /* errno*/

#define MAXTBL 1024

static unsigned int tbl_records = 0;

typedef struct {
  char *path;
  unsigned int pss;
  unsigned int rss;
  unsigned int swap;
  unsigned int swappss;
}mem_table;

void updaterecord( mem_table *t,char * path, unsigned int path_length,
                   unsigned int rss,
                   unsigned int pss,
                   unsigned int swap,
                   unsigned int swappss ){
  int i,f=-1;
  if(path_length > PATH_MAX) path_length = PATH_MAX;
  
  for(i=0;i < tbl_records;i++){
    if( memmem(t[i].path,strlen(t[i].path),path,path_length) != NULL ){
      f=i; //found
      t[i].rss += rss;
      t[i].pss += pss;
      t[i].swap += swap;
      t[i].swappss += swappss;
      break;
    }
  }
  if(f < 0){
    if (tbl_records > MAXTBL-1){
      printf("wmmd error, maximum records reached > %d\n",MAXTBL-1);
      return;
    }
    t[tbl_records].path = strdup(path);
    t[tbl_records].rss += rss;
    t[tbl_records].pss += pss;
    t[tbl_records].swap += swap;
    t[tbl_records].swappss += swappss;
    tbl_records++;
  }
}

int compare_pss (const void * a, const void * b)
{
  return ( ((mem_table*)a)->pss > ((mem_table*)b)->pss );
}

int main(int argc, char **argv){
  
  long i;
  ssize_t nread;

  char mempath[PATH_MAX];
  char buf[sysconf(_SC_PAGESIZE)];
  
  FILE *file;
  char **endint;
  char *start,
       *end,
       *linestart,
       *tmpstart;

  int tmp = 0,
      mempathlen = 0,
      mem_total_rss=0,
      mem_total_pss=0,
      mem_total_swap=0,
      mem_total_swappss=0;

  mem_table tbl[MAXTBL]={0};
  
  if(argc > 1 && (i = strtol(argv[1],endint,10)) > 0){
    sprintf(mempath,"/proc/%ld/smaps",i);
    printf("reading %s\n",mempath);
  }else if(argc > 1 && memmem(argv[1],strlen(argv[1]),"-s",2) != NULL){
    sprintf(mempath,"/proc/self/smaps");
    printf("reading %s\n",mempath);
  }else{
    printf("usage:\n");
    printf("\twmmd <pid>\n");
    printf("\twmmd -s\n");
    printf("\tWhere is My Memory Dude?\n\tThis app will show you real memory usage of process, and what does it consist of.\n");
    exit(0);
  }
  
  setbuf(stdout, NULL);

  int fd = open(mempath, O_RDONLY);
  
  if (fd > 0) {
      while ( (nread = read(fd,buf, sizeof(buf)) ) > 0){
        start = (char *)&buf;
        end   = start + nread;
        do{//parse 4k block
            linestart = start;
            mempathlen = 19;
            sprintf(mempath,"[i'm error of wmmd]");//for error detection, must not be visible for user
            unsigned int pss=0,rss=0,swap=0,swappss=0;

            if( (tmp = (end - start)) > 0 &&
                (start = memmem(start,tmp,"\nSize:",6)) != NULL ) //first property of record
            {
              if((tmp = (start - linestart)) > 0 &&
                (tmpstart = memmem(linestart,tmp,"/",1)) != NULL ) //search path in previous line
                {
                  mempathlen=(start-tmpstart);
                  sprintf(mempath,"%.*s",mempathlen,tmpstart);
                }else{ //search any other description in previous line

                  int flag=0;
                  for(tmpstart = start; tmpstart > linestart; --tmpstart){
                    if(isspace(*tmpstart)){
                      if(flag == 0) continue;
                      tmpstart++;
                      mempathlen=(start-tmpstart);
                      sprintf(mempath,"%.*s",mempathlen,tmpstart);
                      if(mempath[mempathlen-1] == ' ') mempath[--mempathlen]=0;//remove training space
                      if(mempathlen == 1 && mempath[mempathlen-1] == '0'){
                        sprintf(mempath,"[anonymous mmap]");
                        mempathlen=16;
                      }
                      break;
                    }else{
                      flag = 1;//found first non space character
                    }
                  }
                  
                }
            }else
              break; //read next 4k block


            if( (tmp = (end - start)) > 0 &&
                (start = memmem(start,tmp,"\nRss:",5)) != NULL )
            {
              rss = strtol(start+5, &start, 10);
              start += 3;//now we must be at the end of line "Rss:                   0 kB"
            }else
              break; //read next 4k block


            if( (tmp = (end - start)) > 0 &&
                (start = memmem(start,tmp,"\nPss:",5)) != NULL )
            {
              pss = strtol(start+5, &start, 10);
              start += 3;//now we must be at the end of line "Pss:                   0 kB"
            }else
              break; //read next 4k block

            if( (tmp = (end - start)) > 0 &&
                (start = memmem(start,tmp,"\nSwap:",6)) != NULL )
            {
              swap = strtol(start+6, &start, 10);
              start += 3;
            }else
              break;

            if( (tmp = (end - start)) > 0 &&
                (start = memmem(start,tmp,"\nSwapPss:",9)) != NULL )
            {
              swappss = strtol(start+9, &start, 10);
              updaterecord(tbl,mempath,mempathlen,rss,pss,swap,swappss);
              start += 3;
            }else
              break;

        }while(1);

      }
      close(fd);
      qsort (tbl, tbl_records, sizeof(mem_table), compare_pss);
      mem_total_pss = 0;
      mem_total_rss = 0;
      mem_total_swap = 0;
      mem_total_swappss = 0;
      printf("pss     \trss     \tswap    \tswappss \t\n");
      for(i=0;i < tbl_records;i++){
        printf("%8dK\t%8dK\t%8dK\t%8dK\t%s\n",tbl[i].pss,tbl[i].rss,tbl[i].swap,tbl[i].swappss,tbl[i].path);
        mem_total_pss+=tbl[i].pss;
        mem_total_rss+=tbl[i].rss;
        mem_total_swap+=tbl[i].swap;
        mem_total_swappss+=tbl[i].swappss;
      }
      printf("total:\n");
      printf("pss     \trss     \tswap    \tswappss \t\n");
      printf("%8dK\t%8dK\t%8dK\t%8dK\n",mem_total_pss,mem_total_rss,mem_total_swap,mem_total_swappss);
  }else{
    printf("Unable to open file %s , %s\n",mempath,strerror(errno));
  }
}

