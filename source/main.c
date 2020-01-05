/*
*   This is very quickly made example of how to copy a file in 2 threads.
*
*   The reason youd want to do this is because normally when you read a file into a buffer
*   you cannot read again until it has finished writing that buffer to the output file.
*
*   The idea with threads is that you will *always* be writing, and quite often always reading.
*   The reason i say always writing is because in general, writing is slower that reading.
*   So the read thread will always finish first with a new buffer for the write funtion to write from.
*
*   However, its not that simple.
*   We just learned that the read thread will finish first, then the write thread will start.
*   Well if we read into the same buffer again whilst the write function is writing to a file,
*   then bad bad things will happen...
*
*   To get around this, we will use a mutex lock / unlock, which will lock access to the data.
*   Then we will unclock the mutex once we are done using it in that thread.
*
*   In general, the data shared between threads always needs to be protected. 
*/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>


/*
*   I have defined the input file and the output file.
*   Also defined the buffer size, which is 8MiB.
*/
#define INFILE  "infile"
#define OUTFILE "outfile"
#define BUFSIZE 0x800000

typedef struct
{
    void *data;
    size_t data_size;
    size_t data_written;
    size_t total_size;
} thread_t;

/*
*   We create a mutex that will be locked.
*   The file mtx is for the read / write functions.
*/
mtx_t file_mtx;

/*
*   Our conditional vars.
*/
cnd_t can_write;
cnd_t can_read;


//  Gets the file size. Returns 0 on error.
size_t get_file_size(const char *file)
{
    if (!file) return 0;

    FILE *fp = fopen(file, "rb");
    if (!fp) return 0;

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    fclose(fp);
    return size;
}

//  The read thread function.
int thrd_read(void *in)
{
    thread_t *t = (thread_t *)in;
    if(!t) return -1;

    FILE *fp = fopen(INFILE, "rb");
    if (!fp) return -1;

    void *buf = malloc(BUFSIZE);

    for (uint64_t done = 0, bufsize = BUFSIZE; done < t->total_size; done += bufsize)
    {
        if (done + bufsize > t->total_size)
            bufsize = t->total_size - done;

        fread(buf, bufsize, 1, fp);

        mtx_lock(&file_mtx);
        if (t->data_size != 0)
        {
            cnd_wait(&can_read, &file_mtx);
        }

        memcpy(t->data, buf, bufsize);
        t->data_size = bufsize;

        mtx_unlock(&file_mtx);
        cnd_signal(&can_write);
    }

    free(buf);
    fclose(fp);
    return 0;
}

//  The write thread function.
int thrd_write(void *in)
{
    thread_t *t = (thread_t *)in;
    if (!t) return 1;

    FILE *fp = fopen(OUTFILE, "wb");
    if (!fp) return -1;

    while (t->data_written < t->total_size)
    {
        mtx_lock(&file_mtx);
        if (t->data_size == 0)
        {
            cnd_wait(&can_write, &file_mtx);
        }

        fwrite(t->data, t->data_size, 1, fp);
        t->data_written += t->data_size;
        t->data_size = 0;

        mtx_unlock(&file_mtx);
        cnd_signal(&can_read);
    }

    fclose(fp);
    return 0;
}

bool init_app(void)
{
    if (!mtx_init(&file_mtx, mtx_plain)) return false;
    if (!cnd_init(&can_read)) return false;
    if (!cnd_init(&can_write)) return false;
    return true;
}

void exit_app(void)
{
    cnd_destroy(&can_read);
    cnd_destroy(&can_write);
    mtx_destroy(&file_mtx);
}

int main(int argc, char *argv[])
{
    if (!init_app())
    {
        goto jmp_exit;
    }

    size_t file_size = get_file_size(INFILE);
    if (!file_size)
    {
        goto jmp_exit;
    }

    thread_t thread_struct;
    thread_struct.data = malloc(BUFSIZE);
    thread_struct.data_size = 0;
    thread_struct.data_written = 0;
    thread_struct.total_size = file_size;

    thrd_t t_read;
    thrd_t t_write;

    thrd_create(&t_read, thrd_read, &thread_struct);
    thrd_create(&t_write, thrd_write, &thread_struct);

    thrd_join(t_read, NULL);
    thrd_join(t_write, NULL);
    free(thread_struct.data);

    jmp_exit:
    exit_app();
    return 0;
}