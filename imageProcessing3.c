#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <png.h>
#include <omp.h>

#define IMAGESNO 4000
typedef struct {
    int width;
    int height;
    png_byte color_type;
    png_byte bit_depth;
    png_bytep *row_pointers;
} ImageData;

int maskDimensions;
int **maskMatrix;
ImageData images[IMAGESNO];      
ImageData blurImages[IMAGESNO]; 
ImageData negateImages[IMAGESNO]; 


void custom_warning_handler(png_structp png_ptr, png_const_charp warning_msg) {
    // Suppress warning messages
}
void free_image_data(ImageData *img) {
//printf("Freeing\n");
    for (int y = 0; y < img->height; y++) {
        free(img->row_pointers[y]);
        //printf("Freeing2\n");
    }
    free(img->row_pointers);
    img->row_pointers = NULL;
}

void copy_image_data(ImageData *src, ImageData *dest) {
    dest->width = src->width;
    dest->height = src->height;
    dest->color_type = src->color_type;
    dest->bit_depth = src->bit_depth;

    dest->row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * dest->height);
    for (int y = 0; y < dest->height; y++) {
        dest->row_pointers[y] = (png_byte*)malloc(dest->width * 4); 
        memcpy(dest->row_pointers[y], src->row_pointers[y], dest->width * 4);
    }
}


void read_png_file(char *filename, ImageData *img) {
    FILE *fp = fopen(filename, "rb");
    
    if (!fp) {
        printf("File not found: %s. Skipping...\n", filename);
        return;
    }
    
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
        png, info,
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


void maskRGB(ImageData *img) {
    int maskD = maskDimensions * maskDimensions;
    int size = img->height * img->width;
    int *maskedImageR = (int*)malloc(size * sizeof(int));
    int *maskedImageG = (int*)malloc(size * sizeof(int));
    int *maskedImageB = (int*)malloc(size * sizeof(int));

    #pragma omp parallel for
    for (int i = 0; i < img->height; i++) {
        for (int j = 0; j < img->width; j++) {
            int averageR = 0, averageG = 0, averageB = 0;

            for (int itImageX = i - maskDimensions / 2, itMaskX = 0;
                 itImageX <= (i + maskDimensions / 2) && itMaskX < maskDimensions;
                 itImageX++, itMaskX++) {

                if (itImageX >= 0 && itImageX < img->height) {
                    png_bytep row = img->row_pointers[itImageX];
                    for (int itImageY = j - maskDimensions / 2, itMaskY = 0;
                         itImageY <= (j + maskDimensions / 2) && itMaskY < maskDimensions;
                         itImageY++, itMaskY++) {

                        if (itImageY >= 0 && itImageY < img->width) {
                            png_bytep px = &(row[itImageY * 4]);
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


void initialize_mask() {
    maskMatrix = (int**)malloc(maskDimensions * sizeof(int*));
    for (int i = 0; i < maskDimensions; i++) {
        maskMatrix[i] = (int*)malloc(maskDimensions * sizeof(int));
        for (int j = 0; j < maskDimensions; j++) {
            maskMatrix[i][j] = 1;
        }
    }
}

int main() {
    char* fileDirectory = "../DataSet";
    char command[256];

    snprintf(command, sizeof(command), "test -d \"%s\"", fileDirectory);
    if (system(command)) {
        printf("DataSet Doesn't exist\n");
        return 1;
    }
    omp_set_num_threads(1);
    printf("Executing on %d thread\n",omp_get_num_threads());
    
    printf("Enter the size of Mask:\n");
    scanf("%d", &maskDimensions);
    
    system("mkdir -p NegationImages");
    system("mkdir -p BlurImages");
    
    double startTime = omp_get_wtime();

    #pragma omp parallel for
    for (int i = 0; i < IMAGESNO; i++) {
        char fileName[100];
        sprintf(fileName, "%s/%d.png", fileDirectory, i + 1);
        printf("%d ",i+1);
        read_png_file(fileName, &images[i]);
        copy_image_data(&images[i], &negateImages[i]);
        copy_image_data(&images[i], &blurImages[i]);
    }
    for (int i = 0; i < IMAGESNO; i++) {
      free_image_data(&images[i]);
    }
    
    
    initialize_mask();
    double readTime = omp_get_wtime() - startTime;
    printf("Time Taken for reading Images: %.4lf seconds, and %.2lf minutes\n",readTime,readTime/60.0);
    
    startTime = omp_get_wtime();
    
    #pragma omp parallel sections
    {
        #pragma omp section
        {
            for (int i = 0; i < IMAGESNO; i++) {
                if(negateImages[i].width == 0) continue;
                invert_colors(&negateImages[i]);
                char outputFilename[100];
                //printf("Working on negation\n");
                sprintf(outputFilename, "NegationImages/%d.png", i + 1);
                write_png_file(outputFilename, &negateImages[i]);
            }
        }

        #pragma omp section
        {
            for (int i = 0; i < IMAGESNO; i++) {
                if(blurImages[i].width == 0) continue;
                maskRGB(&blurImages[i]);
                char outputFilename[100];
                //printf("Working on bluring\n");
                sprintf(outputFilename, "BlurImages/%d.png", i + 1);
                write_png_file(outputFilename, &blurImages[i]);
            }
        }
    }
    
    for (int i = 0; i < IMAGESNO; i++) {
      free_image_data(&negateImages[i]);
      free_image_data(&blurImages[i]);
    }

    double processTime = omp_get_wtime() - startTime;
    printf("Time Taken for processing Images: %.4lf seconds, and %.2lf minutes\n",processTime,processTime/60.0);
    
    printf("Total Execution time: %.2lf minutes\n",(readTime + processTime)/60.0);

    for (int i = 0; i < maskDimensions; i++) {
        free(maskMatrix[i]);
    }
    free(maskMatrix);
    

    return 0;
}
