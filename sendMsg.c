#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "utils.h"
#define BUFFER_HEAD_LEN 20
#define MAX_COUNT 400

extern MYSQL *mysql;

const char *query_sql = "SELECT t.triggerid,FROM_UNIXTIME(t.lastchange),a.name,t.description,t.priority,t.state FROM triggers t, functions c, items b, hosts a WHERE t.triggerid=c.triggerid AND c.itemid=b.itemid AND b.hostid=a.hostid and NOT EXISTS (SELECT NULL FROM functions f,items i,hosts h WHERE t.triggerid=f.triggerid AND f.itemid=i.itemid AND i.hostid=h.hostid AND (i.status<>0 OR h.status<>0)) AND t.status=0 AND t.value='1' AND t.flags IN ('0','4') ORDER BY t.lastchange asc;";

int total_count = 0;
char triggerid[MAX_COUNT][32];
char name[MAX_COUNT][512];
char description[MAX_COUNT][1024];
char lastchange[MAX_COUNT][32];
char priority[MAX_COUNT][20];

int restart_myself(char **argv)
{
    if (execv(argv[0], argv)) {
        /* ERROR, handle this yourself */
        return -1;
    }
    return 0;
}

void init_list()
{
  total_count = 0;
  int i = 0;
  for(i=0; i<MAX_COUNT; i++)
  {
    memset(triggerid[i], 0, 32);
    memset(name[i], 0, 512);
    memset(description[i], 0, 1024);
    memset(lastchange[i], 0, 32);
    memset(priority[i], 0, 20);
  }
}

int getCurrentTimestamp()
{
  time_t t;
  return time(&t);
}

int send_data_to_server()
{
  int i=0;
  int len = 0;
  int nRet = 0;
  char data_to_send[1024+BUFFER_HEAD_LEN];
  char len_tmp[BUFFER_HEAD_LEN] = {0};

  char *host_name_index = 0;
  char before_host_name[1024] = {0};
  
  //clear table
  sprintf(data_to_send, "%20ustart", 5);
  if (0>=send_sock(data_to_send, BUFFER_HEAD_LEN+5))
  {
      nRet = -1;
  }

  for (i=0;i<total_count;i++)
  {
     memset(data_to_send, 0, sizeof(data_to_send));
     if((host_name_index = strstr(description[i], "{HOST.NAME}"))!= NULL)
     {
       strncpy(before_host_name, description[i], host_name_index-description[i]);
       sprintf(data_to_send+BUFFER_HEAD_LEN, "%s|%s|%s%s%s|%s", lastchange[i], name[i], before_host_name, name[i], host_name_index+strlen("{HOST.NAME}"), priority[i]);
     }
     else
     {
       sprintf(data_to_send+BUFFER_HEAD_LEN, "%s|%s|%s|%s", lastchange[i], name[i], description[i], priority[i]);
     }
     len = strlen(data_to_send+BUFFER_HEAD_LEN);
     sprintf(len_tmp, "%20u", len);
     memcpy(data_to_send, len_tmp, BUFFER_HEAD_LEN);
//     printf("%s", data_to_send);
     if(0>=send_sock(data_to_send, len+BUFFER_HEAD_LEN))
     {
        nRet = -1;
     }
  }
  sprintf(data_to_send, "%20uend", 3);
  if (0>=send_sock(data_to_send, BUFFER_HEAD_LEN+3))
  {
      nRet = -1;
  }
  return nRet;
}

int update_data()
{
  MYSQL_RES *res_ptr;
  MYSQL_ROW mysql_row;
  int nSendData = 0;
  int nCount = 0;

  int res = mysql_query(mysql, query_sql);
  if (res)
  {
     fprintf(stderr, "SELECT error: %s\n", mysql_error(mysql));
     return -1;
  }
  else
  {
     res_ptr = mysql_store_result(mysql);
     if (res_ptr)
     {
       nCount = mysql_num_rows(res_ptr);
       if(total_count != nCount)
       {
          nSendData = 1;
       }

       if(nCount <= 0)
       {
         total_count = 0;
         mysql_free_result(res_ptr);
         return nSendData;
       }

       total_count = 0;
       while ((mysql_row = mysql_fetch_row(res_ptr)) && total_count <= MAX_COUNT)
       {
         if (strcmp(mysql_row[0], triggerid[total_count]) != 0)
         {
           strcpy(triggerid[total_count], mysql_row[0]);
           strcpy(name[total_count], mysql_row[2]);
           strcpy(description[total_count], mysql_row[3]);
           strcpy(lastchange[total_count], mysql_row[1]);
           strcpy(priority[total_count], mysql_row[4]);
           nSendData = 1;
  //         printf("%s,%s,%s,%s,%s\n", triggerid[total_count], name[total_count], description[total_count], lastchange[total_count], priority[total_count]);
         }
         total_count++;
       }

       if (mysql_errno(mysql))
       {
         fprintf(stderr, "Retrive error: %s\n", mysql_error(mysql));
         nSendData = -1;
       }
       mysql_free_result(res_ptr);
     }
  }
  return nSendData;
}

int main(int argc, char ** argv)
{
  char server_ip[64];
  int server_port = -1;

  char mysql_ip[64]={0};
  int mysql_port = -1;
  char mysql_username[512] = {0};
  char mysql_pass[512] = {0};
  char mysql_db[512] = {0};
  int time_to_send_interval = 1000;

  int time_interval = 5;
  int tmp = 0;

  int error_time = 0;
 
  if(argc<9)
  {
    printf("use age<path><server ip><server port><mysql ip><mysql user name><mysql password><mysql port><mysql database><time to send interval(ms)>\n");
    return -1;
  }

  strcpy(server_ip, argv[1]);
  server_port = atoi(argv[2]);
  strcpy(mysql_ip, argv[3]);
  strcpy(mysql_username, argv[4]);
  strcpy(mysql_pass, argv[5]);
  mysql_port = atoi(argv[6]);
  strcpy(mysql_db, argv[7]);
  time_to_send_interval = atoi(argv[8]);
  printf("server_ip:%s, port:%d, mysql ip:%s, mysql user name:%s, mysql port:%d, mysql db:%s\n", 
        server_ip, server_port, mysql_ip, mysql_username, mysql_port, mysql_db);

  
  init_list();

  for (;;)
  {
    if(error_time>10)
    {
      restart_myself(argv);
      printf("restart process\n");
      return 0;
    }

    if (0!=connect_db(mysql_ip, mysql_port, mysql_db, mysql_username, mysql_pass))
    {
        printf("connect mysql faild: server=%s,port=%d,username=%s,database=%s failed(%s)\n",
            mysql_ip, mysql_port, mysql_username, mysql_db, strerror(errno));
        sleep(time_interval);
        error_time++;
        continue;
    }

    if (0!=connect_sock(server_ip, server_port))
    {
       printf("connect socket failed: server:%s, port:%d\n", server_ip, server_port);
       disconnect_db();
       sleep(time_interval);
       error_time++;
       continue;
    }
    printf("---------------\n");
    for(;;)
    {
       tmp = update_data();
       if(tmp < 0)
       {
          sleep(time_interval);
          error_time++;
          break;
       } 
       else if(tmp == 1)
       {
         if(0>send_data_to_server())
         {
            total_count = -1;
            sleep(time_interval);
            error_time++;
            break;
         }
       }
       usleep(time_to_send_interval*1000);
    }
    disconnect_sock();
    disconnect_db();
    sleep(1);
  }
  return 0;
}
