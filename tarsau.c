#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_DOSYA 32
#define MAX_BOYUT (200LL * 1024 * 1024)
#define HEADER_ON_BAYT 10

typedef struct {
    char ad[256];
    int  izin;
    long boyut;
} DosyaBilgi;

int metin_mi(const char *yol) {
    FILE *f = fopen(yol, "rb");
    if (!f) return 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        unsigned char uc = (unsigned char)c;
        /* ASCII kontrol karakterleri hariç (tab, newline, carriage return) */
        if (uc < 32 && uc != '\n' && uc != '\r' && uc != '\t') {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return 1;
}

void dizin_olustur(const char *yol) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", yol);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

void bundle(int argc, char *argv[]) {
    char *cikti = "a.sau";
    char *girdi[MAX_DOSYA];
    int girdi_sayisi = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 < argc) cikti = argv[++i];
        } else {
            if (girdi_sayisi >= MAX_DOSYA) {
                fprintf(stderr, "Hata: en fazla %d dosya!\n", MAX_DOSYA);
                exit(1);
            }
            girdi[girdi_sayisi++] = argv[i];
        }
    }

    if (girdi_sayisi == 0) {
        fprintf(stderr, "Hata: giriş dosyası belirtilmedi!\n");
        exit(1);
    }

    DosyaBilgi bilgi[MAX_DOSYA];
    long toplam_boyut = 0;

    for (int i = 0; i < girdi_sayisi; i++) {
        if (!metin_mi(girdi[i])) {
            printf("%s giriş dosyasının formatı uyumsuzdur!\n", girdi[i]);
            exit(1);
        }
        struct stat st;
        if (stat(girdi[i], &st) != 0) {
            fprintf(stderr, "Hata: %s okunamadı!\n", girdi[i]);
            exit(1);
        }
        toplam_boyut += st.st_size;
        if (toplam_boyut > MAX_BOYUT) {
            fprintf(stderr, "Hata: 200 MB sınırı aşıldı!\n");
            exit(1);
        }
        strncpy(bilgi[i].ad, girdi[i], 255);
        bilgi[i].ad[255] = '\0';
        bilgi[i].izin = st.st_mode & 0777;
        bilgi[i].boyut = st.st_size;
    }

    char header[4096] = "";
    for (int i = 0; i < girdi_sayisi; i++) {
        char kayit[512];
        snprintf(kayit, sizeof(kayit), "|%s,%o,%ld|",
                 bilgi[i].ad, bilgi[i].izin, bilgi[i].boyut);
        strncat(header, kayit, sizeof(header) - strlen(header) - 1);
    }

    int header_icerik_uzunluk = (int)strlen(header);
    int toplam_header = HEADER_ON_BAYT + header_icerik_uzunluk;

    FILE *cikti_f = fopen(cikti, "wb");
    if (!cikti_f) {
        fprintf(stderr, "Hata: %s açılamadı!\n", cikti);
        exit(1);
    }

    fprintf(cikti_f, "%010d", toplam_header);
    fputs(header, cikti_f);

    for (int i = 0; i < girdi_sayisi; i++) {
        FILE *f = fopen(girdi[i], "rb");
        if (!f) {
            fprintf(stderr, "Hata: %s açılamadı!\n", girdi[i]);
            fclose(cikti_f);
            exit(1);
        }
        char buf[4096];
        size_t okunan;
        while ((okunan = fread(buf, 1, sizeof(buf), f)) > 0)
            fwrite(buf, 1, okunan, cikti_f);
        fclose(f);
    }

    fclose(cikti_f);
    printf("Dosyalar birleştirildi.\n");
}

void extract(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Kullanım: tarsau -a arsiv.sau [dizin]\n");
        exit(1);
    }

    char *arsiv_yol = argv[2];
    char *hedef_dizin = (argc >= 4) ? argv[3] : ".";

    size_t uzunluk = strlen(arsiv_yol);
    if (uzunluk < 4 || strcmp(arsiv_yol + uzunluk - 4, ".sau") != 0) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        exit(1);
    }

    FILE *f = fopen(arsiv_yol, "rb");
    if (!f) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        exit(1);
    }

    char on_bayt[11] = {0};
    if (fread(on_bayt, 1, HEADER_ON_BAYT, f) != HEADER_ON_BAYT) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        fclose(f);
        exit(1);
    }

    int header_toplam = atoi(on_bayt);
    int header_icerik = header_toplam - HEADER_ON_BAYT;

    if (header_icerik <= 0) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        fclose(f);
        exit(1);
    }

    char *header = malloc(header_icerik + 1);
    if (!header) { fclose(f); exit(1); }

    if (fread(header, 1, header_icerik, f) != (size_t)header_icerik) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        free(header);
        fclose(f);
        exit(1);
    }
    header[header_icerik] = '\0';

    DosyaBilgi bilgi[MAX_DOSYA];
    int dosya_sayisi = 0;

    char *p = header;
    while (*p == '|' && dosya_sayisi < MAX_DOSYA) {
        p++;
        char *son = strchr(p, '|');
        if (!son) break;
        *son = '\0';
        char ad[256];
        int izin;
        long boyut;
        if (sscanf(p, "%255[^,],%o,%ld", ad, &izin, &boyut) == 3) {
            strncpy(bilgi[dosya_sayisi].ad, ad, 255);
            bilgi[dosya_sayisi].ad[255] = '\0';
            bilgi[dosya_sayisi].izin  = izin;
            bilgi[dosya_sayisi].boyut = boyut;
            dosya_sayisi++;
        }
        p = son + 1;
    }
    free(header);

    dizin_olustur(hedef_dizin);

    for (int i = 0; i < dosya_sayisi; i++) {
        char tam_yol[512];
        snprintf(tam_yol, sizeof(tam_yol), "%s/%s", hedef_dizin, bilgi[i].ad);

        FILE *cikti = fopen(tam_yol, "wb");
        if (!cikti) {
            fprintf(stderr, "Hata: %s yazılamadı!\n", tam_yol);
            fclose(f);
            exit(1);
        }

        long kalan = bilgi[i].boyut;
        char buf[4096];
        while (kalan > 0) {
            size_t okunacak = (kalan < 4096) ? (size_t)kalan : 4096;
            size_t okunan = fread(buf, 1, okunacak, f);
            if (okunan == 0) break;
            fwrite(buf, 1, okunan, cikti);
            kalan -= (long)okunan;
        }
        fclose(cikti);
        chmod(tam_yol, bilgi[i].izin);
    }

    fclose(f);
    printf("%s dizininde dosyalar açıldı.\n", hedef_dizin);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Kullanım:\n");
        fprintf(stderr, "  tarsau -b dosya1 dosya2 -o arsiv.sau\n");
        fprintf(stderr, "  tarsau -a arsiv.sau [dizin]\n");
        return 1;
    }
    if (strcmp(argv[1], "-b") == 0) {
        bundle(argc, argv);
    } else if (strcmp(argv[1], "-a") == 0) {
        extract(argc, argv);
    } else {
        fprintf(stderr, "Geçersiz parametre: %s\n", argv[1]);
        return 1;
    }
    return 0;
}

