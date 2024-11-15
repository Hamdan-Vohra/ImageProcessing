#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <png.h>
#include <omp.h>

#define IMAGESNO 4265

typedef struct {
    int width;
    int height;
    png_byte color_type;
    png_byte bit_depth;
    png_bytep *row_pointers;
} ImageData;

int maskDimensions;
int **maskMatrix;
ImageData images[IMAGESNO];  // Array to store all images' data

void custom_warning_handler(png_structp png_ptr, png_const_charp warning_msg) {
    // Do nothing, effectively ignoring the warning
}

void read_png_file(char *filename, ImageData *img) {
    FILE *fp = fopen(filename, "rb");

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) abort();

    png_set_error_fn(png, NULL, NULL, custom_warning_handler);

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

    fclose(fp);
    png_destroy_write_struct(&png, &info);
}

void maskRGB(ImageData *img) {
    int size = img->height * img->width;
    int maskD = maskDimensions * maskDimensions;
    int averageR, averageG, averageB;

    int *maskedImageR = (int*)malloc(size * sizeof(int));
    int *maskedImageG = (int*)malloc(size * sizeof(int));
    int *maskedImageB = (int*)malloc(size * sizeof(int));

    for (int i = 0; i < img->height; i++) {
        for (int j = 0; j < img->width; j++) {
            averageR = averageG = averageB = 0;

            for (int itImageX = i - maskDimensions / 2, itMaskX = 0;
                 itImageX <= (i + maskDimensions / 2) && itMaskX < maskDimensions;
                 itImageX++, itMaskX++) {
                if (itImageX >= 0 && itImageX < img->height) {
                    png_bytep row = img->row_pointers[itImageX];
                    for (int itImageY = j - maskDimensions / 2, itMaskY = 0;
                         itImageY <= (j + maskDimensions / 2) && itMaskY < maskDimensions;
                         itImageY++, itMaskY++) {
                        png_bytep px = &(row[itImageY * 4]);
                        if (itImageY >= 0 && itImageY < img->width) {
                            averageR += px[0] * maskMatrix[itMaskX][itMaskY];
                            averageG += px[1] * maskMatrix[itMaskX][itMaskY];
                            averageB += px[2] * maskMatrix[itMaskX][itMaskY];
                        }
                    }
                }
            }
            maskedImageR[i * img->width + j] = averageR / maskD;
            maskedImageG[i * img->width + j] = averageG / maskD;
            maskedImageB[i * img->width + j] = averageB / maskD;
        }
    }

    int x = 0;
    for (int i = 0; i < img->height; i++) {
        png_bytep row = img->row_pointers[i];
        for (int j = 0; j < img->width; j++) {
            png_bytep px = &(row[j * 4]);
            px[0] = maskedImageR[x];
            px[1] = maskedImageG[x];
            px[2] = maskedImageB[x];
            x++;
        }
    }

    free(maskedImageR);
    free(maskedImageG);
    free(maskedImageB);
}

void process_png_file(ImageData *img) {
    maskMatrix = (int**)malloc(maskDimensions * sizeof(int*));
    for (int i = 0; i < maskDimensions; i++) {
        maskMatrix[i] = (int*)malloc(maskDimensions * sizeof(int));
    }
    for (int i = 0; i < maskDimensions; i++) {
        for (int j = 0; j < maskDimensions; j++)
            maskMatrix[i][j] = 1;
    }
    maskRGB(img);

    for (int i = 0; i < maskDimensions; i++) {
        free(maskMatrix[i]);
    }
    free(maskMatrix);
}

int main() {
    printf("\nEnter the size of Mask:\n");
    scanf("%d", &maskDimensions);

    system("mkdir -p BlurImages");

    double startReadTime = omp_get_wtime();


    for (int i = 0; i < IMAGESNO; i++) {
        char inputFilename[100];
        sprintf(inputFilename, "DataSet/%d.png", i + 1);
        read_png_file(inputFilename, &images[i]);
    }

    double endReadTime = omp_get_wtime();
    
    printf("Time taken for Reading Images is %lf\n", endReadTime - startReadTime);
    
    double startProcessTime = omp_get_wtime();
    
    for (int i = 0; i < IMAGESNO; i++) {
        process_png_file(&images[i]);
    }

    for (int i = 0; i < IMAGESNO; i++) {
        char outputFilename[100];
        sprintf(outputFilename, "BlurImages/%d.png", i + 1);
        write_png_file(outputFilename, &images[i]);

        // Free row pointers for each image after writing
        for (int y = 0; y < images[i].height; y++) {
            free(images[i].row_pointers[y]);
        }
        free(images[i].row_pointers);
    }

    double endProcessTime = omp_get_wtime();    
    printf("Time taken for Bluring Images: %lf\n", endProcessTime - startProcessTime);

    printf("Total Time taken: %lf\n", (endProcessTime - startProcessTime) + (endReadTime - startReadTime));
    return 0;
}

