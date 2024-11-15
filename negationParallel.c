#include <stdlib.h>
#include <stdio.h>
#include <png.h>
#include <omp.h>
#include <string.h>

#define IMAGESNO 60

typedef struct {
    int width;
    int height;
    png_byte color_type;
    png_byte bit_depth;
    png_bytep *row_pointers;
} ImageData;

ImageData img[IMAGESNO];  

void read_png_file(char *filename, ImageData *img) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) abort();

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) abort();

    png_infop info = png_create_info_struct(png);
    if (!info) abort();

    if (setjmp(png_jmpbuf(png))) abort();

    png_init_io(png, fp);
    png_read_info(png, info);

    img->width = png_get_image_width(png, info);
    img->height = png_get_image_height(png, info);
    img->color_type = png_get_color_type(png, info);
    img->bit_depth = png_get_bit_depth(png, info);

    if (img->bit_depth == 16) png_set_strip_16(png);
    if (img->color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (img->color_type == PNG_COLOR_TYPE_GRAY && img->bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (img->color_type == PNG_COLOR_TYPE_RGB || img->color_type == PNG_COLOR_TYPE_GRAY || img->color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (img->color_type == PNG_COLOR_TYPE_GRAY || img->color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    img->row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * img->height);
    for (int y = 0; y < img->height; y++) {
        img->row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png, info));
    }
    png_read_image(png, img->row_pointers);

    fclose(fp);
    png_destroy_read_struct(&png, &info, NULL);
}

void write_png_file(char *filename, ImageData *img) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) abort();

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) abort();

    png_infop info = png_create_info_struct(png);
    if (!info) abort();

    if (setjmp(png_jmpbuf(png))) abort();

    png_init_io(png, fp);

    png_set_IHDR(
        png,
        info,
        img->width, img->height,
        8,
        PNG_COLOR_TYPE_RGBA,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );
    png_write_info(png, info);
    png_write_image(png, img->row_pointers);
    png_write_end(png, NULL);

    for (int y = 0; y < img->height; y++) {
        free(img->row_pointers[y]);
    }
    free(img->row_pointers);

    fclose(fp);
    png_destroy_write_struct(&png, &info);
}

void invert_colors(ImageData *img) {
    #pragma omp parallel for
    for (int y = 0; y < img->height; y++) {
        png_bytep row = img->row_pointers[y];
        for (int x = 0; x < img->width; x++) {
            png_bytep px = &(row[x * 4]);
            px[0] = 255 - px[0];
            px[1] = 255 - px[1];
            px[2] = 255 - px[2];
        }
    }
}

int main() {

    system("mkdir -p NegationImages");
    
    double totalReadTime = 0.0;
    double totalProcessWriteTime = 0.0;

    // Read images (separate read loop)
    double readStart = omp_get_wtime();
    for (int i = 0; i < IMAGESNO ; i++) {
        char inputFilename[25];
        sprintf(inputFilename, "DataSet/%d.png", i + 1);
        read_png_file(inputFilename, &img[i]);
    }
    double readEnd = omp_get_wtime();
    totalReadTime = readEnd - readStart;

    // Process images (separate process loop)
    double processStart = omp_get_wtime();
    for (int i = 0; i < IMAGESNO; i++) {
        invert_colors(&img[i]);
    }
    double processEnd = omp_get_wtime();

    // Write images (separate write loop)
    double writeStart = omp_get_wtime();
    for (int i = 0; i < IMAGESNO; i++) {
        char outputFilename[25];
        sprintf(outputFilename, "NegationImages/%d.png", i + 1 );
        write_png_file(outputFilename, &img[i]);
    }
    double writeEnd = omp_get_wtime();
    totalProcessWriteTime = (processEnd - processStart) + (writeEnd - writeStart);

    printf("Total Read Time: %lf seconds\n", totalReadTime);
    printf("Total Process and Write Time: %lf seconds\n", totalProcessWriteTime);

    return 0;
}

