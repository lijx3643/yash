#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <string.h>
#include <fcntl.h>
#include <malloc.h>

int pipefd[2];
int pid_ch1;
int pid_ch2;
int wpid;
char currlist[2]={'-','+'};
char *statlist[3]={"Running","Stopped","Done"};


/*
  curr: '-' for previous, '+' for current
  stat: 0 for Running, 1 for Stopped, 2 for Done
  pgid: record process group id of the job
*/
typedef struct job{
  int jobid;
  char *jobstring;
  int curr;
  int stat;
  pid_t pgid;
  struct job *next;
} LinkedList;


//keep track of head and endnode in the LinkedList
LinkedList *head;
LinkedList *endnode;


//insert a new node after endnode, return it as endnode
LinkedList *insert(LinkedList *endnode,int jobid, char *jobstring){
  LinkedList *node;
  node=(LinkedList*)malloc(sizeof(LinkedList));
  node->jobid=jobid;
  node->jobstring=jobstring;
  node->curr=1;
  node->stat=0;
  endnode->next=node;
  node->next=NULL;
  return node;
}


/*
  delete a node with certain jobid
  t: previous node 
  f: delete node
  return its previous node t
  keep track of the end if we delete the endnode
*/
LinkedList *delete(LinkedList *head,int jobid){
  LinkedList *t=head;
  LinkedList *f;
  while(jobid!=t->next->jobid && t->next!=NULL){
    t=t->next;
  }
  if(t->next!=NULL){
    f=t->next;
    t->next=f->next;
    free(f);
    if(t!=head && t->next==NULL)
      t->curr=1;//change curr if t becomes endnode
    return t;
  }
}


//print job in required format
void printjob(LinkedList *node){
  printf("[%d] %c    %s     %s\n",node->jobid,currlist[node->curr],statlist[node->stat],node->jobstring);
}


/*
  operate on process from token[start] to token[end]
*/
void process(char *token[],int start,int end){
  //set redirection
  //len: the length of tokens before redirection
  int len=end-start;
  for(int i=start;i<end;i++){
    if (strcmp(token[i],">")==0){
      len=len<(i-start)?len:(i-start);
      int fd=open(token[i+1],O_RDWR|O_CREAT,S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
      dup2(fd,1);
    }
    else if (strcmp(token[i],"<")==0){
      len=len<(i-start)?len:(i-start);
      int fd=open(token[i+1],O_RDONLY,S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
      //if input file does not exit, return fail command
      if(fd>0){
        dup2(fd,0);
      }
      else{
	exit(EXIT_FAILURE);
      }
    }
    else if (strcmp(token[i],"2>")==0){
      len=len<(i-start)?len:(i-start);
      int fd=open(token[i+1],O_RDWR|O_CREAT,S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
      dup2(fd,2);
    }
  }

  //get command, parameter
  if(strcmp(token[end-1],"&")==0){
    //if jobstring contains "&", do not copy it to para
    char *para[len];
    for (int i=0;i<len;i++){
      para[i]=token[start+i];
    }
    para[len-1]=NULL;
    char *command=token[start];
    //execvp
    execvp(command,para);
    exit(EXIT_FAILURE);
  }
  else{
    //copy all
    char *para[len+1];
    for (int i=0;i<len;i++){
      para[i]=token[start+i];
    }
    para[len]=NULL;
    char *command=token[start];
    //execvp
    execvp(command,para);
    exit(EXIT_FAILURE);
  }
}


/*
  parse instring into tokens
  end: return the length of tokens
*/
int parse(char *parsestr,char *token[]){
  int end=0;
  char *temp=strtok(parsestr," ");
  while(temp!=NULL){
    strcpy(token[end++],temp);
    temp=strtok(NULL," ");
  }
  return end;
}

/*
  sighandler for SIGINT and SIGTSTP
  operate on fg process, which must be the endnode
*/
void sigint_handler(int signum){
  if(endnode!= head && endnode!=NULL){
    kill(-(endnode->pgid),SIGINT);
  }
}

void sigtstp_handler(int signum){
  if(endnode!= head && endnode!=NULL){
    kill(-(endnode->pgid),SIGTSTP);
  }
}


/*
  sighandler for background pipe
  collect the exit of bg pipe using wpid
  wpid: receive the pid of both children
  once received the pid_ch1(pgid), print the node and delete it
  keep track of endnode if necessary
*/
void sigpipechld_handler(int signum){
  int status;
  int count;//ckeck if we find the pgid or not
  int wpid=waitpid(-1,&status,WNOHANG);
  while (wpid>0 && count!=1){
    LinkedList *t=head;
    while(t->pgid!=wpid && t!=NULL){
      t=t->next;
    }
    if(t!=NULL && t->pgid==wpid){
      //found pgid, print out, then delete node
      count=1;
      t->stat=2;	  
      printjob(t);
      LinkedList *p=delete(head,t->jobid);
      if(p->next==NULL){
        p->curr=1;
        endnode=p;
      }
    }
  }
}


/*
  sighandler for background process
  once received the pgid, print and delete the node
*/
void sigchld_handler(int signum){
  int status;
  int count=0;
  int wpid=waitpid(-1,&status,WNOHANG);
  while(wpid>0){
    LinkedList *t=head->next;
    while(t->pgid!=wpid && t!=NULL){
      t=t->next;
    }
    if(t->pgid==wpid){
      t->stat=2;	  
      printjob(t);
      LinkedList *p=delete(head,t->jobid);
      if(p->next==NULL){
       p->curr=1;
       endnode=p;
     }
     break;
   }
 }
}


int main(void){
  head=(LinkedList*)malloc(sizeof(LinkedList));
  head->next=NULL;
  endnode=head;
  char *inString;
  int status;

  while(inString=readline("# ")){
    //init token[]
    char *token[30]={NULL};
    for(int k=0;k<30;k++){
      token[k]=(char*)malloc(30*sizeof(char));
    }

    if (strcmp(inString,"fg")==0){
      //find the most recent bg job(stopped or running)
      LinkedList *t=head;
      LinkedList *find=NULL;//the job
      LinkedList *findpre=NULL;//keep its previous node
      while(t->next!=NULL){
        if(t->next->stat!=2){
          findpre=t;
          find=t->next;
        }
        t=t->next;
      }

      //if find the job
      if(find!=NULL){
        kill(-(find->pgid),SIGCONT);
        char *fgstring=find->jobstring;
        //if it is stopped by ^Z
        if(find->stat==1){
          find->stat=0;
        }
        //if it is bg &, then cut off the "&"
        else{
          fgstring=strtok(fgstring,"&");
        }
        printf("%s\n",fgstring);

        wpid=waitpid(-1,&status,WUNTRACED);
        signal(SIGINT,sigint_handler);
        signal(SIGTSTP,sigtstp_handler);
        //if exit or terminated, delete
        if(wpid>0 && (WIFEXITED(status)||WIFSIGNALED(status))){
          findpre->next=find->next;
          free(find);
          if(findpre->next==NULL)
            endnode=findpre;
          findpre->curr=1;
        }
        //if stopped
        if(WIFSTOPPED(status)){
          endnode->stat=1;
        }
      }
    }  

    else if(strcmp(inString,"bg")==0){
      //find the most recent bg job(stopped)
      LinkedList *t=head;
      LinkedList *find=NULL;//the job
      LinkedList *findpre=NULL;//keep its previous node
      while(t->next!=NULL){
        if(t->next->stat==1){
          findpre=t;
          find=t->next;
        }
        t=t->next;
      }
      
      //if find the job
      if(find!=NULL){
        find->stat=0;
        kill(-(find->pgid),SIGCONT);
        printf("[%d] %c     %s &\n",find->jobid,currlist[find->curr],find->jobstring);
        signal(SIGCHLD,sigchld_handler);
      }
    }

    else if(strcmp(inString,"jobs")==0){
      LinkedList *t=head->next;
      while(t!=NULL){
        printjob(t);
        t=t->next;
      }
    }

    else{
      //keep inString from destroyed
      //strtok in parse function will destroy the parsestr
      char jobstr[]="";
      strcpy(jobstr,inString);
      char *parsestr=jobstr;
      int end=parse(parsestr,token);

      //add new job in the list
      //change the curr of prenode to '-'
      if(endnode!=head) endnode->curr=0;
      //currjobid: the highest jobid in the list
      int currjobid=0;
      LinkedList *t=head->next;
      while(t!=NULL){
        if(t->jobid>currjobid) currjobid=t->jobid;
        t=t->next;
      }
      currjobid++;//highest+1
      t=insert(endnode,currjobid,inString);
      endnode=t;

      //pilo: pipe location
      //pilo=0 -> there is no pipe
      //pilo!=0 -> there is pipe
      int pilo=0;
      for(int i=0;i<end;i++){
        if (strcmp(token[i],"|")==0)
          pilo=i;
      }

      //pipe
      if (pilo!=0){
        pipe(pipefd); 
        pid_ch1=fork();
        setpgid(0,0);
        endnode->pgid=pid_ch1;
        if (pid_ch1==0){//child1
          close(pipefd[0]);
          dup2(pipefd[1],1);
          process(token,0,pilo);
        }
        else{
          pid_ch2=fork();
          setpgid(0,endnode->pgid);
          if (pid_ch2==0){//child2
            close(pipefd[1]);
            dup2(pipefd[0],0);
            process(token,pilo+1,end);
          }
          else{//parent
            //if the pipe is bg job with "&"
            if(strstr(endnode->jobstring,"&")!=NULL){
              close(pipefd[0]);
              close(pipefd[1]);
              //catch its exit
              signal(SIGCHLD,sigpipechld_handler);
            }
            //if the pipe is fg job
            else{
              signal(SIGINT,sigint_handler);
              signal(SIGTSTP,sigtstp_handler);  
              close(pipefd[0]);
              close(pipefd[1]);
              wpid=waitpid(pid_ch1,&status,WUNTRACED|WCONTINUED);
              wpid=waitpid(pid_ch2,&status,WUNTRACED|WCONTINUED);
              //if exit or terminated, delete
              if(WIFEXITED(status)||WIFSIGNALED(status)){
                endnode=delete(head,currjobid);
              }
              //if stopped
              if(WIFSTOPPED(status)){
                endnode->stat=1;
              }
            }
          }
        }
      }

      //there is no pipe
      else if(pilo==0){
        pid_ch1=fork();
        setpgid(0,0);
        endnode->pgid=pid_ch1;
        if (pid_ch1==0){//child
          process(token,0,end);
        }
        else{//parent
          //if it is bg job
          if(strstr(endnode->jobstring,"&")!=NULL){
            signal(SIGCHLD,sigchld_handler);
          }
          //if it is fg job
          else{
            signal(SIGINT,sigint_handler);
            signal(SIGTSTP,sigtstp_handler);
            waitpid(pid_ch1,&status,WUNTRACED|WCONTINUED);
            //if exited or terminated
            if(WIFEXITED(status)||WIFSIGNALED(status)){
              endnode=delete(head,currjobid);
            }
            //if stopped
            if(WIFSTOPPED(status)){
              endnode->stat=1;
            }
          }
        }
      }
    }
  }
}
