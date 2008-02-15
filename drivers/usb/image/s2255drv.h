/*
 * Sensoray 2255 USB Linux driver
 *
 * Copyright (C) 2007-2008 by Sensoray Company Inc.
 *                            Dean Anderson
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License as
 *      published by the Free Software Foundation, version 2.
 */

#ifndef S2255DRIVER_H
#define S2255DRIVER_H


#define DIR_IN				0
#define DIR_OUT				1
/* firmware query */
#define VX_FW 			        0x30

#define MAX_CHANNELS 4
#define FRAME_MARKER   0x2255DA4AL
#define MAX_PIPE_USBBLOCK     (40*1024)
#define DEFAULT_PIPE_USBBLOCK (16*1024)
#define MAX_CHANNELS    4
#define MAX_PIPE_BUFFERS 1
#define SYS_FRAMES              4
/* maximum size is PAL full size plus room for the marker header(s) */
#define SYS_FRAMES_MAXSIZE     (720*288*2*2 + 4096)
#define DEF_USB_BLOCK       (4096)
#define LINE_SZ_4CIFS_NTSC  640
#define LINE_SZ_2CIFS_NTSC  640
#define LINE_SZ_1CIFS_NTSC  320
#define LINE_SZ_4CIFS_PAL   704
#define LINE_SZ_2CIFS_PAL   704
#define LINE_SZ_1CIFS_PAL   352
#define NUM_LINES_4CIFS_NTSC 240
#define NUM_LINES_2CIFS_NTSC 240
#define NUM_LINES_1CIFS_NTSC 240
#define NUM_LINES_4CIFS_PAL  288
#define NUM_LINES_2CIFS_PAL  288
#define NUM_LINES_1CIFS_PAL  288
#define LINE_SZ_DEF          640
#define NUM_LINES_DEF        240


/* predefined settings */
#define FORMAT_NTSC    1
#define FORMAT_PAL     2

#define SCALE_4CIFS    1 /*640x480(NTSC) or 704x576(PAL)*/
#define SCALE_2CIFS    2 /*640x240(NTSC) or 704x288(PAL)*/
#define SCALE_1CIFS    3 /*320x240(NTSC) or 352x288(PAL)*/

#define COLOR_YUVPL    1 /*YUV planar*/
#define COLOR_YUVPK    2 /*YUV packed*/
#define COLOR_RGB      3 /*RGB */
#define COLOR_Y8       4 /*monochrome*/

/* frame decimation. Not implemented by V4L yet(experimental in V4L) */
#define FDEC_1     1     /* capture every frame. default */
#define FDEC_2     2     /* capture every 2nd frame */
#define FDEC_3     3     /* capture every 3rd frame */
#define FDEC_5     5     /* capture every 5th frame */

/*-------------------------------------------------------
 * Default mode parameters.
 *-------------------------------------------------------*/
#define DEF_SCALE               SCALE_4CIFS
#define DEF_COLOR               COLOR_YUVPL
#define DEF_FDEC                FDEC_1
#define DEF_BRIGHT              0
#define DEF_CONTRAST            0x5c
#define DEF_SATURATION          0x80
#define DEF_HUE                 0

/* usb config commands */
#define IN_DATA_TOKEN  0x2255c0de
#define CMD_2255       0xc2255000
#define CMD_SET_MODE  (CMD_2255 | 0x10)
#define CMD_START     (CMD_2255 | 0x20)
#define CMD_STOP      (CMD_2255 | 0x30)
#define CMD_STATUS    (CMD_2255 | 0x40)

struct mode2255i {
        u32   format;     /* input video format (NTSC, PAL) */
        u32   scale;      /* output video scale */
        u32   color;      /* output video color format */
        u32   fdec;       /* frame decimation */
        u32   bright;     /* brightness */
        u32   contrast;   /* contrast */
        u32   saturation; /* saturation */
        u32   hue;        /*hue (NTSC only)*/
        u32   single;     /*capture 1 frame at a time (!=0), continuously (==0)*/
        u32   usb_block;  /* block size. should be 4096 of DEF_USB_BLOCK */
        u32   restart;    /* if DSP requires restart */
};

/* frame structure */
#define FRAME_STATE_UNUSED  0
#define FRAME_STATE_FILLING 1
#define FRAME_STATE_FULL    2


struct framei {
        unsigned long size;

        unsigned long ulState;  /* ulState ==0 unused, 1 being filled, 2 full */
        void *lpvbits;          /* image data */
        unsigned long cur_size; /* current data copied to it */
};

/* image buffer structure */
struct bufferi {
        unsigned long dwFrames;             /* number of frames in buffer; */
        struct framei frame[SYS_FRAMES];    /* array of FRAME structures; */
};


#define DEF_MODEI_NTSC_CONT  FORMAT_NTSC, DEF_SCALE, DEF_COLOR, \
                DEF_FDEC, DEF_BRIGHT, DEF_CONTRAST, DEF_SATURATION, \
                DEF_HUE, 0, DEF_USB_BLOCK,0

#define DEF_MODEI_PAL_CONT   FORMAT_PAL, DEF_SCALE,  DEF_COLOR, DEF_FDEC,\
                DEF_BRIGHT, DEF_CONTRAST, DEF_SATURATION, DEF_HUE, 0, \
                DEF_USB_BLOCK,0

#define DEF_MODEI_NTSC_SING  FORMAT_NTSC, DEF_SCALE, DEF_COLOR, DEF_FDEC,\
                DEF_BRIGHT, DEF_CONTRAST, DEF_SATURATION, DEF_HUE, 1,\
                DEF_USB_BLOCK,0

#define DEF_MODEI_PAL_SING   FORMAT_PAL, DEF_SCALE,  DEF_COLOR, DEF_FDEC, \
                DEF_BRIGHT, DEF_CONTRAST, DEF_SATURATION, DEF_HUE, 1,\
                DEF_USB_BLOCK,0

struct s2255_dmaqueue {
        struct list_head       active;
        struct list_head       queued;
        struct timer_list      timeout;
        // thread for acquisition
        struct task_struct     *kthread;
        wait_queue_head_t      wq;
        int                    frame;
        struct s2255_dev       *dev;
        int                    channel;
};

/* for firmware loading */
#define FWSTATE_NOTLOADED 0
#define FWSTATE_SUCCESS   1
#define FWSTATE_FAILED    2

typedef struct complete_data {
        int    fw_loaded;
        int    fw_size;
        struct urb *fw_urb;
        int    fw_state;
        void   *pfw_data;
} complete_data_t;

struct s2255_pipeinfo;


typedef struct s2255_pipeinfo
{
        u32                 max_transfer_size;
        u32                 cur_transfer_size;
        u8*                 pTransferBuffer;
        u32                 transfer_flags;;
        u32                 state;
        u32                 prev_state;
        u32                 urb_size;
        void *              pStreamUrb;
        void *              dev;  /* back pointer to s2255_dev struct*/
        u32                 err_count;
        u32                 buf_index;
        u32                 idx;
        u32                 priority_set;
} s2255_pipeinfo_t;



struct s2255_dev {
        int                    frames;
        int                    users[MAX_CHANNELS];
        struct mutex           lock;
        int                    resources[MAX_CHANNELS];
        struct usb_device      *udev;
        struct usb_interface   *interface;
        u8                     read_endpoint;
        struct semaphore       sem_frms[MAX_CHANNELS]; /* frames ready */
        struct s2255_dmaqueue  vidq[MAX_CHANNELS];
        struct video_device    *vdev[MAX_CHANNELS];
        struct list_head s2255_devlist;
        struct timer_list timer;
        struct complete_data   *fw_data;
        int                     board_num;
        int                     is_open;
        struct s2255_pipeinfo   UsbPipes[MAX_PIPE_BUFFERS];
        struct bufferi          buffer[MAX_CHANNELS];
        struct mode2255i        mode[MAX_CHANNELS];
        int                     cur_frame[MAX_CHANNELS];
        int                     last_frame[MAX_CHANNELS];
        u32                     cc; /* current channel */
        int                     b_acquire[MAX_CHANNELS];
        unsigned long           req_image_size[MAX_CHANNELS];
        int                     bad_payload[MAX_CHANNELS];
        unsigned long           frame_count[MAX_CHANNELS];
        int                     frame_ready;
        struct kref             kref;
};


struct s2255_fmt {
        char  *name;
        u32   fourcc;
        int   depth;
};


/* buffer for one video frame */
struct s2255_buffer {
        /* common v4l buffer stuff -- must be first */
        struct videobuf_buffer vb;
        const struct s2255_fmt *fmt;
        /* future use */
        int reserved[32];

};


struct s2255_fh {
        struct s2255_dev           *dev;
        unsigned int               resources;
        const struct s2255_fmt     *fmt;
        unsigned int               width,height;
        struct videobuf_queue      vb_vidq;
        enum v4l2_buf_type         type;
        int                        channel;
};




#endif
