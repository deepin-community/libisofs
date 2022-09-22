
/*
 * Copyright (c) 2007 - 2016 Vreixo Formoso, Thomas Schmitt
 * 
 * This file is part of the libisofs project; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License version 2 
 * or later as published by the Free Software Foundation. 
 * See COPYING file for details.
 */

static char helptext[][80] = {
"",
"This is a collection of libisofs gestures which formerly were distinct",
"programs. The first argument chooses the gesture:",
"  -tree  absolute_directory_path",
"               Import a directory and print the resulting iso tree.",
"  -find  absolute_directory_path",      
"               Import a directory, find matching nodes and print the",
"               resulting iso tree.",
"  -iso  [options] directory output_file",
"               Create an iso image from a local directory. For options see",
"               output of -iso -h",
"  -iso_read  image_file",
"               Output the contents of an iso image.",
"  -iso_cat  image_file path_in_image",
"               Extract a file from a given ISO image and put out its content",
"               to stdout. The file is addressed by path_in_image. The ISO",
"               image does not get loaded but rather the lookups are done",
"               directly in the image file.",
"  -iso_modify  image_file absolute_directory_path output_file",
"               Load an iso image, add a directory, and write complete image.",
"  -iso_ms  image_lba nwa image_file directory_path output_file",
"               Load an iso image, add a directory, and write as add-on",
"               session which shall be appended to the old image.",
"               image_lba gives the block address of the start of the most",
"               recent session in the image_file. nwa gives the block address",
"               where the add-on session will be appended to the image.",
"@"
};


#define LIBISOFS_WITHOUT_LIBBURN yes
#include "libisofs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <err.h>
#include <limits.h>
#include <errno.h>


#ifndef PATH_MAX
#define PATH_MAX Libisofs_default_path_maX
#endif


/* ----------------------------- utilities -------------------------- */


void demo_report_iso_err(int err, char *occasion)
{
    char *severity;

    fprintf(stderr, "%s : err = 0x%X", occasion, (unsigned int) err);
    if (err < 0) {
        iso_sev_to_text(iso_error_get_severity(err), &severity);
        fprintf(stderr, " -> %s '%s'", severity, iso_error_to_msg(err));
    }
    fprintf(stderr, "\n");
}


/* ------------------------- from demo/tree.c ----------------------- */

static void
print_permissions(mode_t mode)
{
    char perm[10];
    
    /* TODO suid, sticky... */
    
    perm[9] = '\0';
    perm[8] = mode & S_IXOTH ? 'x' : '-';
    perm[7] = mode & S_IWOTH ? 'w' : '-';
    perm[6] = mode & S_IROTH ? 'r' : '-';
    perm[5] = mode & S_IXGRP ? 'x' : '-';
    perm[4] = mode & S_IWGRP ? 'w' : '-';
    perm[3] = mode & S_IRGRP ? 'r' : '-';
    perm[2] = mode & S_IXUSR ? 'x' : '-';
    perm[1] = mode & S_IWUSR ? 'w' : '-';
    perm[0] = mode & S_IRUSR ? 'r' : '-';
    printf("[%s]",perm);
}

static void 
tree_print_dir(IsoDir *dir, int level) 
{
    int i;
    IsoDirIter *iter;
    IsoNode *node;
    char *sp;

    sp = calloc(1, level * 2 + 1);
    
    for (i = 0; i < level * 2; i += 2) {
        sp[i] = '|';
        sp[i+1] = ' ';
    }
    
    if (level > 0)
        sp[level * 2 - 1] = '-';
    sp[level * 2] = '\0';
    
    iso_dir_get_children(dir, &iter);
    while (iso_dir_iter_next(iter, &node) == 1) {
        
        if (ISO_NODE_IS_DIR(node)) {
            printf("%s+[D] ", sp);
            print_permissions(iso_node_get_permissions(node));
            printf(" %s\n", iso_node_get_name(node));
            tree_print_dir(ISO_DIR(node), level+1);
        } else if (ISO_NODE_IS_FILE(node)) {
            printf("%s-[F] ", sp);
            print_permissions(iso_node_get_permissions(node));
            printf(" %s\n", iso_node_get_name(node) );
        } else if (ISO_NODE_IS_SYMLINK(node)) {
            printf("%s-[L] ", sp);
            print_permissions(iso_node_get_permissions(node));
            printf(" %s -> %s \n", iso_node_get_name(node),
                   iso_symlink_get_dest(ISO_SYMLINK(node)) );
        } else {
            printf("%s-[C] ", sp);
            print_permissions(iso_node_get_permissions(node));
            printf(" %s\n", iso_node_get_name(node) );
        } 
    }
    iso_dir_iter_free(iter);
    free(sp);
}

int gesture_tree(int argc, char **argv)
{
    int result;
    IsoImage *image;
    
    if (argc != 2) {
need_abs_path:;
        fprintf (stderr, "You need to specify a valid absolute path\n");
        return 1;
    }
    if (argv[1][0] != '/')
        goto need_abs_path;

    iso_init();
    iso_set_msgs_severities("NEVER", "ALL", "");
    
    result = iso_image_new("volume_id", &image);
    if (result < 0) {
        printf ("Error creating image\n");
        return 1;
    }
    
    result = iso_tree_add_dir_rec(image, iso_image_get_root(image), argv[1]);
    if (result < 0) {
        printf ("Error adding directory %d\n", result);
        return 1;
    }
    
    printf("================= IMAGE =================\n");
    tree_print_dir(iso_image_get_root(image), 0);
    printf("\n\n");
    
    iso_image_unref(image);
    iso_finish();
    return 0;
}


/* ------------------------- from demo/find.c ----------------------- */

static void 
find_print_dir(IsoDir *dir) 
{
    IsoDirIter *iter;
    IsoNode *node;
    IsoFindCondition *cond, *c1, *c2;
    
    c1 = iso_new_find_conditions_name("*a*");
    c2 = iso_new_find_conditions_mode(S_IFREG);
    cond = iso_new_find_conditions_and(c1, c2);
    iso_dir_find_children(dir, cond, &iter);
    while (iso_dir_iter_next(iter, &node) == 1) {
        char *path = iso_tree_get_node_path(node);
        printf(" %s\n", path);
        free(path);
    }
    iso_dir_iter_free(iter);
}

int gesture_find(int argc, char **argv)
{
    int result;
    IsoImage *image;
    
    if (argc != 2) {
need_abs_path:;
        fprintf (stderr, "You need to specify a valid absolute path\n");
        return 1;
    }
    if (argv[1][0] != '/')
        goto need_abs_path;

    iso_init();
    iso_set_msgs_severities("NEVER", "ALL", "");
    
    result = iso_image_new("volume_id", &image);
    if (result < 0) {
        printf ("Error creating image\n");
        return 1;
    }
    
    result = iso_tree_add_dir_rec(image, iso_image_get_root(image), argv[1]);
    if (result < 0) {
        printf ("Error adding directory %d\n", result);
        return 1;
    }
    
    find_print_dir(iso_image_get_root(image));
    
    iso_image_unref(image);
    iso_finish();
    return 0;
}


/* ------------------------- from demo/iso.c ----------------------- */


static const char * const optstring = "JRIL:b:hV:";
extern char *optarg;
extern int optind;

void iso_usage(char **argv)
{
    printf("%s [OPTIONS] DIRECTORY OUTPUT\n", argv[0]);
}

void iso_help()
{
    printf(
        "Options:\n"
        "  -J        Add Joliet support\n"
        "  -R        Add Rock Ridge support\n"
        "  -I        Add ISO 9660:1999 support\n"
        "  -V label  Volume Label\n"
        "  -L <num>  Set the ISO level (1 or 2)\n"
        "  -b file   Specifies a boot image to add to image\n"
        "  -h        Print this message\n"
    );
}

int iso_callback(IsoFileSource *src)
{
    char *path = iso_file_source_get_path(src);
    printf("CALLBACK: %s\n", path);
    free(path);
    return 1;
}

int gesture_iso(int argc, char **argv)
{
    int result;
    int c;
    IsoImage *image;
    struct burn_source *burn_src;
    unsigned char buf[2048];
    FILE *fp = NULL;
    IsoWriteOpts *opts;
    char *volid = "VOLID";
    char *boot_img = NULL;
    int rr = 0, j = 0, iso1999 = 0, level = 1;

    while ((c = getopt(argc, argv, optstring)) != -1) {
        switch(c) {
        case 'h':
            iso_usage(argv);
            iso_help();
            goto ex;
            break;
        case 'J':
            j = 1;
            break;
        case 'R':
            rr = 1;
            break;
        case 'I':
            iso1999 = 1;
            break;
        case 'L':
            level = atoi(optarg);
            break;
        case 'b':
            boot_img = optarg;
            break;
        case 'V':
            volid = optarg;
            break;
        case '?':
            iso_usage(argv);
            goto ex;
            break;
        }
    }

    if (argc < 2) {
        printf ("Please pass directory from which to build ISO\n");
        iso_usage(argv);
        goto ex;
    }
    if (argc < 3) {
        printf ("Please supply output file\n");
        iso_usage(argv);
        goto ex;
    }

    fp = fopen(argv[optind+1], "w");
    if (fp == NULL) {
        err(1, "error opening output file");
        goto ex;
    }

    result = iso_init();
    if (result < 0) {
        printf ("Can't initialize libisofs\n");
        goto ex;
    }
    iso_set_msgs_severities("NEVER", "ALL", "");

    result = iso_image_new(volid, &image);
    if (result < 0) {
        printf ("Error creating image\n");
        goto ex;
    }
    iso_tree_set_follow_symlinks(image, 0);
    iso_tree_set_ignore_hidden(image, 0);
    iso_tree_set_ignore_special(image, 0);
    iso_set_abort_severity("SORRY");
    /*iso_tree_set_report_callback(image, callback);*/

    result = iso_tree_add_dir_rec(image, iso_image_get_root(image),
                                  argv[optind]);
    if (result < 0) {
        printf ("Error adding directory %d\n", result);
        goto ex;
    }

    if (boot_img) {
        /* adds El-Torito boot info. Tunned for isolinux */
        ElToritoBootImage *bootimg;
        result = iso_image_set_boot_image(image, boot_img, ELTORITO_NO_EMUL,
                                     "/isolinux/boot.cat", &bootimg);
        if (result < 0) {
            printf ("Error adding boot image %d\n", result);
            goto ex;
        }
        el_torito_set_load_size(bootimg, 4);
        el_torito_patch_isolinux_image(bootimg);
    }

    result = iso_write_opts_new(&opts, 0);
    if (result < 0) {
        printf ("Cannot create write opts, error %d\n", result);
        goto ex;
    }
    iso_write_opts_set_iso_level(opts, level);
    iso_write_opts_set_rockridge(opts, rr);
    iso_write_opts_set_joliet(opts, j);
    iso_write_opts_set_iso1999(opts, iso1999);

    result = iso_image_create_burn_source(image, opts, &burn_src);
    if (result < 0) {
        printf ("Cannot create image, error %d\n", result);
        goto ex;
    }

    iso_write_opts_free(opts);

    while (burn_src->read_xt(burn_src, buf, 2048) == 2048) {
        result = fwrite(buf, 1, 2048, fp);
        if (result < 2048) {
            printf ("Cannot write block. errno= %d\n", errno);
            goto ex;
        }
    }
    fclose(fp);
    burn_src->free_data(burn_src);
    free(burn_src);

    iso_image_unref(image);
    iso_finish();
    return 0;
ex:;
    if (fp != NULL)
      fclose(fp);
    return 1;
}


/* ------------------------- from demo/iso_read.c ----------------------- */


static void
iso_read_print_type(mode_t mode)
{
    switch(mode & S_IFMT) {
    case S_IFSOCK: printf("[S] "); break;
    case S_IFLNK: printf("[L] "); break;
    case S_IFREG: printf("[R] "); break;
    case S_IFBLK: printf("[B] "); break;
    case S_IFDIR: printf("[D] "); break;
    case S_IFIFO: printf("[F] "); break;
    }
}

static void
iso_read_print_file_src(IsoFileSource *file)
{
    struct stat info;
    char *name;
    iso_file_source_lstat(file, &info);
    iso_read_print_type(info.st_mode);
    print_permissions(info.st_mode);
    printf(" %10.f ", (double) info.st_size);
    /* printf(" {%ld,%ld} ", (long)info.st_dev, (long)info.st_ino); */
    name = iso_file_source_get_name(file);
    printf(" %s", name);
    free(name);
    if (S_ISLNK(info.st_mode)) {
        char buf[PATH_MAX];
        iso_file_source_readlink(file, buf, PATH_MAX);
        printf(" -> %s\n", buf);
    }
    printf("\n");
}

static void
iso_read_print_dir(IsoFileSource *dir, int level)
{
    int ret, i;
    IsoFileSource *file;
    struct stat info;
    char *sp;

    sp = calloc(1, level * 2 + 1);

    for (i = 0; i < level * 2; i += 2) {
        sp[i] = '|';
        sp[i+1] = ' ';
    }

    if (level > 0)
        sp[level * 2 - 1] = '-';
    sp[level * 2] = '\0';

    ret = iso_file_source_open(dir);
    if (ret < 0) {
        printf ("Can't open dir %d\n", ret);
    }
    while ((ret = iso_file_source_readdir(dir, &file)) == 1) {
        printf("%s", sp);
        iso_read_print_file_src(file);
        ret = iso_file_source_lstat(file, &info);
        if (ret < 0) {
            break;
        }
        if (S_ISDIR(info.st_mode)) {
            iso_read_print_dir(file, level + 1);
        }
        iso_file_source_unref(file);
    }
    iso_file_source_close(dir);
    if (ret < 0) {
        printf ("Can't print dir\n");
    }
    free(sp);
}

int gesture_iso_read(int argc, char **argv)
{
    int result, initialized = 0, return_val = 1;
    IsoImageFilesystem *fs = NULL;
    IsoDataSource *src = NULL;
    IsoFileSource *root = NULL;
    IsoReadOpts *ropts = NULL;

    if (argc != 2) {
        printf ("You need to specify a valid path\n");
        goto ex;
    }

    result = iso_init();
    if (result < 0) {
        demo_report_iso_err(result, "Cannot init libisofs");
        goto ex;
    }
    initialized = 1;

    iso_set_msgs_severities("NEVER", "ALL", "");

    result = iso_data_source_new_from_file(argv[1], &src);
    if (result < 0) {
        demo_report_iso_err(result, "Error creating data source");
        goto ex;
    }
    result = iso_read_opts_new(&ropts, 0);
    if (result < 0) {
        demo_report_iso_err(result, "Error creating read options");
        goto ex;
    }
    result = iso_image_filesystem_new(src, ropts, 1, &fs);
    if (result < 0) {
        demo_report_iso_err(result, "Error creating filesystem");
        goto ex;
    }
    iso_read_opts_free(ropts);
    ropts = NULL;

    printf("\nVOLUME INFORMATION\n");
    printf("==================\n\n");

    printf("Vol. id: %s\n", iso_image_fs_get_volume_id(fs));
    printf("Publisher: %s\n", iso_image_fs_get_publisher_id(fs));
    printf("Data preparer: %s\n", iso_image_fs_get_data_preparer_id(fs));
    printf("System: %s\n", iso_image_fs_get_system_id(fs));
    printf("Application: %s\n", iso_image_fs_get_application_id(fs));
    printf("Copyright: %s\n", iso_image_fs_get_copyright_file_id(fs));
    printf("Abstract: %s\n", iso_image_fs_get_abstract_file_id(fs));
    printf("Biblio: %s\n", iso_image_fs_get_biblio_file_id(fs));

    printf("\nDIRECTORY TREE\n");
    printf("==============\n");

    result = fs->get_root(fs, &root);
    if (result < 0) {
        demo_report_iso_err(result, "Cannot get root object");
        goto ex;
    }
    /* iso_read_print_file_src(root); */
    iso_read_print_dir(root, 0);

    return_val = 0;
ex:;
    if (root != NULL)
        iso_file_source_unref(root);
    if (ropts != NULL)
        iso_read_opts_free(ropts);
    if (fs != NULL) {
        fs->close(fs);
        iso_filesystem_unref((IsoFilesystem*)fs);
    }
    if (src != NULL)
        iso_data_source_unref(src);
    if (initialized)
        iso_finish();
    return return_val;
}


/* ------------------------- from demo/iso_cat.c ----------------------- */


int gesture_iso_cat(int argc, char **argv)
{
    int res, write_ret, ret;
    IsoFilesystem *fs = NULL;
    IsoFileSource *file = NULL;
    struct stat info;
    IsoDataSource *src = NULL;
    IsoReadOpts *opts = NULL;

    if (argc != 3) {
        fprintf(stderr, "Usage: -iso_cat /path/to/image /path/to/file\n");
        return 1;
    }

    res = iso_init();
    if (res < 0) {
        demo_report_iso_err(res, "Cannot init libisofs");
        return 1;
    }

    /* Important Note:
       From here on memory objects get created which need to be freed in
       the end. Therefore in case of problems no direct return, but rather
       a hop to label "ex:", where cleanup happens.
     */

    res = iso_data_source_new_from_file(argv[1], &src);
    if (res < 0) {
        demo_report_iso_err(res, "Error creating data source object");
        ret = 1; goto ex;
    }

    res = iso_read_opts_new(&opts, 0);
    if (res < 0) {
        demo_report_iso_err(res, "Error creating read options object");
        ret = 1; goto ex;
    }
    res = iso_image_filesystem_new(src, opts, 1, &fs);
    if (res < 0) {
        demo_report_iso_err(res, "Error creating filesystem object");
        ret = 1; goto ex;
    }
    iso_read_opts_free(opts);
    opts = NULL;

    res = fs->get_by_path(fs, argv[2], &file);
    if (res < 0) {
        demo_report_iso_err(res, "Cannot get file object with given path");
        ret = 1; goto ex;
    }

    res = iso_file_source_lstat(file, &info);
    if (res < 0) {
        demo_report_iso_err(res,
                         "Cannot inquire type of file object with given path");
        ret = 1; goto ex;
    }

    if (S_ISDIR(info.st_mode)) {
        fprintf(stderr, "Path refers to a directory!!\n");
        ret = 1; goto ex;
    } else {
        char buf[1024];
        res = iso_file_source_open(file);
        if (res < 0) {
            demo_report_iso_err(res,
                                "Cannot open file object with given path");
            ret = 1; goto ex;
        }
        while ((res = iso_file_source_read(file, buf, 1024)) > 0) {
            write_ret = fwrite(buf, 1, res, stdout);
            if (write_ret < res) {
                printf ("Cannot write block to stdout. errno= %d\n", errno);
                iso_file_source_close(file);
                ret = 1; goto ex;
            }
        }
        iso_file_source_close(file);
        if (res < 0) {
            demo_report_iso_err(res, "Error while reading data content");
            fprintf(stderr, "Error reading, err = 0x%X\n", (unsigned int) res);
            ret = 1; goto ex;
        }
    }

    ret = 0;
ex:;
    if (file != NULL)
        iso_file_source_unref(file);
    if (fs != NULL)
        iso_filesystem_unref(fs);
    if (opts != NULL)
        iso_read_opts_free(opts);
    if (src != NULL)
        iso_data_source_unref(src);
    iso_finish();
    return ret;
}


/* ------------------------- from demo/iso_modify.c ----------------------- */


void iso_modify_usage(char **argv)
{
    printf("%s IMAGE DIRECTORY OUTPUT\n", argv[0]);
}

int gesture_iso_modify(int argc, char **argv)
{
    int result, return_val = 1, initialized = 0;
    IsoImage *image = NULL;
    IsoDataSource *src = NULL;
    struct burn_source *burn_src = NULL;
    unsigned char buf[2048];
    FILE *fp = NULL;
    IsoWriteOpts *opts = NULL;
    IsoReadOpts *ropts = NULL;
	
    if (argc < 4) {
        iso_modify_usage(argv);
        goto ex;
    }
    
    fp = fopen(argv[3], "w");
    if (fp == NULL) {
        err(1, "error opening output file");
        goto ex;
    }

    result = iso_init();
    if (result < 0) {
        demo_report_iso_err(result, "Cannot init libisofs");
        goto ex;
    }
    initialized = 1;
    iso_set_msgs_severities("NEVER", "ALL", "");
    
    /* create the data source to accesss previous image */
    result = iso_data_source_new_from_file(argv[1], &src);
    if (result < 0) {
        demo_report_iso_err(result, "Error creating data source");
        goto ex;
    }
    
    /* create the image context */
    result = iso_image_new("volume_id", &image);
    if (result < 0) {
        demo_report_iso_err(result, "Error creating image");
        goto ex;
    }
    iso_tree_set_follow_symlinks(image, 0);
    iso_tree_set_ignore_hidden(image, 0);
    
    /* import previous image */
    result = iso_read_opts_new(&ropts, 0);
    if (result < 0) {
        demo_report_iso_err(result, "Error creating read options");
        goto ex;
    }
    result = iso_image_import(image, src, ropts, NULL);
    if (result < 0) {
        demo_report_iso_err(result, "Error importing previous session");
        goto ex;
    }
    /* (One could of course keep them alive until cleanup) */
    iso_read_opts_free(ropts);
    ropts = NULL;
    iso_data_source_unref(src);
    src = NULL;
    
    /* add new dir */
    result = iso_tree_add_dir_rec(image, iso_image_get_root(image), argv[2]);
    if (result < 0) {
        demo_report_iso_err(result, "Error adding directory");
        goto ex;
    }
    
    /* Generate a new image with both previous and added contents.
       Profile 1 means Rock Ridge and ISO level 3.
    */
    result = iso_write_opts_new(&opts, 1);
    if (result < 0) {
        demo_report_iso_err(result, "Cannot create write opts");
        goto ex;
    }
    /* Prefer specs violation over relocation deep directories */
    iso_write_opts_set_allow_deep_paths(opts, 1);

    /* For MS-Windows readers : iso_write_opts_set_joliet(opts, 1); */
 
    result = iso_image_create_burn_source(image, opts, &burn_src);
    if (result < 0) {
        demo_report_iso_err(result, "Cannot create image object");
        goto ex;
    }
    iso_write_opts_free(opts);
    opts = NULL;
    
    while (burn_src->read_xt(burn_src, buf, 2048) == 2048) {
        result = fwrite(buf, 1, 2048, fp);
        if (result < 2048) {
            fprintf (stderr, "Cannot write block. errno= %d\n", errno);
            goto ex;
        }
    }

    return_val = 0;
ex:
    if (fp != NULL)
        fclose(fp);
    if (opts != NULL)
        iso_write_opts_free(opts);
    if (burn_src != NULL) {
        burn_src->free_data(burn_src);
        free(burn_src);
    }
    if (image != NULL)
        iso_image_unref(image);
    if (ropts != NULL)
        iso_read_opts_free(ropts);
    if (src != NULL)
        iso_data_source_unref(src);
    if (initialized)
        iso_finish();
    return return_val;
}


/* ------------------------- from demo/iso_ms.c ----------------------- */


void iso_ms_usage(char **argv)
{
    printf("%s LSS NWA DISC DIRECTORY OUTPUT\n", argv[0]);
}

int gesture_iso_ms(int argc, char **argv)
{
    int result, return_val = 1, initialized = 0;
    IsoImage *image = NULL;
    IsoDataSource *src = NULL;
    struct burn_source *burn_src = NULL;
    unsigned char buf[2048];
    FILE *fp = NULL;
    IsoWriteOpts *opts = NULL;
    IsoReadOpts *ropts = NULL;
    uint32_t ms_block;
	
    if (argc < 6) {
        iso_ms_usage(argv);
        goto ex;
    }

    if (strcmp(argv[3], argv[5]) == 0) {
        fprintf(stderr,
                "image_file and output_file must not be the same file.\n");
        goto ex;
    }

    fp = fopen(argv[5], "w");
    if (!fp) {
        err(1, "error opening output file");
        goto ex;
    }

    result = iso_init();
    if (result < 0) {
        demo_report_iso_err(result, "Cannot init libisofs");
        goto ex;
    }
    initialized = 1;

    iso_set_msgs_severities("NEVER", "ALL", "");
    
    /* create the data source to accesss previous image */
    result = iso_data_source_new_from_file(argv[3], &src);
    if (result < 0) {
        demo_report_iso_err(result, "Error creating data source");
        goto ex;
    }
    
    /* create the image context */
    result = iso_image_new("volume_id", &image);
    if (result < 0) {
        demo_report_iso_err(result, "Error creating image");
        goto ex;
    }
    iso_tree_set_follow_symlinks(image, 0);
    iso_tree_set_ignore_hidden(image, 0);
    
    /* import previous image */
    result = iso_read_opts_new(&ropts, 0);
    if (result < 0) {
        fprintf(stderr, "Error creating read options\n");
        goto ex;
    }
    iso_read_opts_set_start_block(ropts, atoi(argv[1]));
    result = iso_image_import(image, src, ropts, NULL);
    iso_read_opts_free(ropts);
    ropts = NULL;
    iso_data_source_unref(src);
    src = NULL;
    if (result < 0) {
        demo_report_iso_err(result, "Error importing previous session");
        goto ex;
    }
    
    /* add new dir */
    result = iso_tree_add_dir_rec(image, iso_image_get_root(image), argv[4]);
    if (result < 0) {
        demo_report_iso_err(result, "Error adding directory");
        goto ex;
    }
    
    /* generate a multisession image with new contents */
    result = iso_write_opts_new(&opts, 1);
    if (result < 0) {
        demo_report_iso_err(result, "Cannot create write opts");
        goto ex;
    }
    
    /* round up to 32kb aligment = 16 block */
    ms_block =  atoi(argv[2]);
    iso_write_opts_set_ms_block(opts, ms_block);
    iso_write_opts_set_appendable(opts, 1);

    result = iso_image_create_burn_source(image, opts, &burn_src);
    if (result < 0) {
        printf ("Cannot create image, error %d\n", result);
        goto ex;
    }
    iso_write_opts_free(opts);
    opts = NULL;
    
    while (burn_src->read_xt(burn_src, buf, 2048) == 2048) {
        result = fwrite(buf, 1, 2048, fp);
        if (result < 2048) {
            printf ("Cannot write block. errno= %d\n", errno);
            goto ex;
        }
    }

    return_val = 0;
ex:;
    if (burn_src != NULL) {
        burn_src->free_data(burn_src);
        free(burn_src);
    }
    if (opts != NULL)
        iso_write_opts_free(opts);
    if (image)
        iso_image_unref(image);
    if (ropts != NULL)
        iso_read_opts_free(ropts);
    if (src != NULL)
        iso_data_source_unref(src);
    if (initialized)
        iso_finish();
    if (fp != NULL)
        fclose(fp);
    return return_val;
}


/* ------------------------- switcher ----------------------- */


int main(int argc, char **argv)
{
    char *gesture;
    int i;

    if (argc < 2) {
usage:;
        fprintf(stderr, "usage: %s gesture [gesture_options]\n", argv[0]);
        for (i = 0; helptext[i][0] != '@'; i++)
             fprintf(stderr, "%s\n", helptext[i]);
        exit(1);
    }
    for (gesture = argv[1]; *gesture == '-'; gesture++);
    if (strcmp(gesture, "tree") == 0) {
        gesture_tree(argc - 1, &(argv[1]));
    } else if(strcmp(gesture, "find") == 0) {
        gesture_find(argc - 1, &(argv[1]));
    } else if(strcmp(gesture, "iso") == 0) {
        gesture_iso(argc - 1, &(argv[1]));
    } else if(strcmp(gesture, "iso_read") == 0) {
        gesture_iso_read(argc - 1, &(argv[1]));
    } else if(strcmp(gesture, "iso_cat") == 0) {
        gesture_iso_cat(argc - 1, &(argv[1]));
    } else if(strcmp(gesture, "iso_modify") == 0) {
        gesture_iso_modify(argc - 1, &(argv[1]));
    } else if(strcmp(gesture, "iso_ms") == 0) {
        gesture_iso_ms(argc - 1, &(argv[1]));
    } else {
        goto usage;
    }
    exit(0);
}
