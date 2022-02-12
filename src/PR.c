#include <ctype.h>
#include <mpi.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Maksymalna liczba procesów
#define MAX_PROCESY 	64
// Czy czekamy po wydrukowaniu komunikatów
#define SLEEP			1 //0
// Czasy oczekiwania (sekundy)
#define MIN_CZEKAJ		1
#define MAX_CZEKAJ		4
// Rozmiar bufora znakowego
#define	ROZMIAR_BUFORA	64

// Tagi wiadomości
#define MSG_ZETON		1234
#define MSG_ZADANIE		4321

// 'do { } while(0)' by każde makro z wieloma instrukcjami musiało być zakończone średnikiem

// Wyświetlanie komunikatów
#define log(wyjscie, fmt, ...) \
	do {\
		char* znacznik_czasowy = utworz_znacznik_czasowy();\
		fprintf(wyjscie, "%s\t" fmt, znacznik_czasowy, ##__VA_ARGS__);\
		fprintf(plik_logu, "%s\t" fmt, znacznik_czasowy, ##__VA_ARGS__);\
		free(znacznik_czasowy);\
	} while(0)
#define debug(fmt, ...) \
	do {\
		log(stdout, "%s\tP%d\t" fmt "\n", nazwa_hosta, moj_id, ##__VA_ARGS__);\
		spij(MIN_CZEKAJ);\
	} while (0)
#define dbg(fmt, ...) \
	fprintf(stdout, "%d\t%d\t%s\t" fmt "\n", moj_id,  __LINE__, __FUNCTION__, ##__VA_ARGS__)
#define error(fmt, ...) \
	do {\
		log(stderr, "ERROR:\t" fmt "\n", ##__VA_ARGS__);\
		exit(1);\
	} while (0)
#define init_error(fmt, ...) \
	do {\
		if (moj_id == 0)\
			error(fmt, ##__VA_ARGS__);\
		else\
			exit(1);\
	} while (0)

// Blokada zmiennych współdzielonych
#define MUTEX(lock, expr) \
	do {\
	pthread_mutex_lock(lock);\
	expr;\
	pthread_mutex_unlock(lock);\
	} while (0)
#define LOCK(lock) pthread_mutex_lock(lock)
#define UNLOCK(lock) pthread_mutex_unlock(lock)

// Wysyłanie, odbieranie
//Wersje dla MPI Thread Level 3
//#define wyslij(tag, dest, buf, count) MPI_Send(buf, count, MPI_INT, dest, tag, MPI_COMM_WORLD)
//#define odbierz(tag, buf, count, status) MPI_Recv(buf, count, MPI_INT, MPI_ANY_SOURCE, tag, MPI_COMM_WORLD, status)

// Begin MPI Thread Level 0 Thread Support Hack

// Zamek dla operacji MPI
pthread_mutex_t zamek_MPI;

void wyslij(int tag, int dest, void *buf, int count) {
	MPI_Request request;
	int flag = 0;

	pthread_mutex_lock(&zamek_MPI);
	MPI_Isend(buf, count, MPI_INT, dest, tag, MPI_COMM_WORLD, &request);
	pthread_mutex_unlock(&zamek_MPI);
	while (!flag) {
		pthread_mutex_lock(&zamek_MPI);
		MPI_Test(&request, &flag, MPI_STATUS_IGNORE);
		pthread_mutex_unlock(&zamek_MPI);
	}
}

void odbierz(int tag, void *buf, int count, MPI_Status *status) {
	MPI_Request request;
	int flag = 0;

	pthread_mutex_lock(&zamek_MPI);
	MPI_Irecv(buf, count, MPI_INT, MPI_ANY_SOURCE, tag, MPI_COMM_WORLD,
			&request);
	pthread_mutex_unlock(&zamek_MPI);
	while (!flag) {
		pthread_mutex_lock(&zamek_MPI);
		MPI_Test(&request, &flag, status);
		pthread_mutex_unlock(&zamek_MPI);
	}
}

// End MPI Thread Level 0 Thread Support Hack

// Plik logu
FILE *plik_logu;
char sciezka_logu[ROZMIAR_BUFORA];

// Zmienne węzła
int moj_id;
char nazwa_hosta[ROZMIAR_BUFORA];
int dlugosc_nazwy_hosta;

// Zmienne współdzielone wątków (adnotacje - gdzie aktualnie używane)
int mam_zeton; // główny, odbiorca
int *zadal; // główny, odbiorca
int *dostal; // główny, odbiorca
int moj_num; // główny
int w_sekcji_krytycznej; // główny, odbiorca

// Zmienne do sterowania wątkami
int koniec_pracy;
pthread_t watek_glowny, watek_odbiorca;
pthread_mutex_t zamek_mam_zeton, zamek_zadal, zamek_dostal,
		zamek_w_sekcji_krytycznej;

// Parametry zadania
// K komputerów (procesów), N serwisów (stanowisk naprawczych), S poczekalni (stanowisk przyjęć/wydań)
int liczba_komputerow, liczba_serwisow, liczba_poczekalni;
int rozmiar_tablicy_komputerow, rozmiar_tablicy_poczekalni,
		rozmiar_tablicy_serwisu;
int *poczekalnie, *serwisy;
int wybrana_poczekalnia, wybrany_serwis;

enum stan_t {
	SPRAWNY, ZEPSUTY, PRZYJMOWANY, NAPRAWIANY, NAPRAWIONY, WYDAWANY
} stan;
enum kierunek_t {
	WEJSCIE, WYJSCIE
};

// Wypisuje tablicę zmiennych całkowitych
char *opisz_tablice(const int tablica[], int rozmiar, char *opis) {
	char * wynik = (char *) calloc(4 * ROZMIAR_BUFORA, sizeof(char));
	int element;
	strcat(wynik, opis);
	strcat(wynik, " (id, wartosc) : [");
	if (rozmiar != 0) {
		for (element = 0; element < rozmiar; element++) {
			char opis_elementu[ROZMIAR_BUFORA];
			sprintf(opis_elementu, "(%d %d)", element, tablica[element]);
			strcat(wynik, opis_elementu);
			if (element != rozmiar - 1)
				strcat(wynik, ", ");
		}
	}
	strcat(wynik, "]");
	return wynik;
}

// Wyłączanie/Włączanie opóźnień sterowane stałą SLEEP
unsigned int spij(unsigned int seconds) {
	if (SLEEP)
		return sleep(seconds);
	else
		return 0;
}

// Zwraca maximum dwóch liczb całkowitych
int max(int a, int b) {
	return a > b ? a : b;
}

// Zwraca znacznik czasowy
char *utworz_znacznik_czasowy(void) {
	char *wynik = (char*) malloc(ROZMIAR_BUFORA * sizeof(char));
	struct timespec ts;

	clock_gettime(1, &ts);
	snprintf(wynik, ROZMIAR_BUFORA, "%lld\t%ld", (long long) ts.tv_sec,
			ts.tv_nsec);
	return wynik;
}

// Sprawdza czy ciąg znaków jest liczbą całkowitą
int isint(char *str) {
	while (*str != '\0') {
		if (!isdigit(*str))
			return 0;
		str++;
	}
	return 1;
}

// Zwraca losową liczbę całkowitą z przedziału <min, max>
int losowa_liczba(int min, int max) {
	return min + (rand() % (int) (max - min + 1));
}

// Inicjuje zakończenie
void procedura_zakonczenia(int sig) {
	debug("Inicjuję sekwencję zakończenia.");
	koniec_pracy = 1;
}

// Zlicza wystąpienia elementów tablicy o danym rozmiarze o zadanej wartości
int zlicz_wystapienia(int *tablica, int rozmiar_tablicy, int wartosc) {
	int wynik = 0;
	int element;

	for (element = 0; element < rozmiar_tablicy; element++) {
		if (tablica[element] == wartosc)
			wynik++;
	}
	return wynik;
}

// Informuje, czy udało nam się zarezerwować poczekalnię
// Zapewniamy jedno miejsce wolne w poczekalni dla wychodzących z serwisu,
// jeżeli w poczekalni jest tylko jedno miejsce wolne i serwis jest pełny
int zarezerwowalem_poczekalnie(enum kierunek_t kierunek) {
	int wybrana;

	if (zlicz_wystapienia(poczekalnie, liczba_poczekalni, 0) == 1
			&& zlicz_wystapienia(serwisy, liczba_serwisow, 1) == liczba_serwisow
			&& kierunek != WYJSCIE)
		return 0;
	for (wybrana = 0; wybrana < liczba_poczekalni; wybrana++) {
		if (poczekalnie[wybrana] == 0) {
			poczekalnie[wybrana] = 1;
			wybrana_poczekalnia = wybrana;
			return 1;
		}
	}
	return 0;
}

// Informuje, czy udało nam się zarezerwować serwis
int zarezerwuj_serwis(void) {
	int wybrany;

	for (wybrany = 0; wybrany < liczba_serwisow; wybrany++) {
		if (serwisy[wybrany] == 0) {
			serwisy[wybrany] = 1;
			wybrany_serwis = wybrany;
			return 1;
		}
	}
	return 0;
}

// Zwalania poczekalnie
void zwolnij_poczekalnie(void) {
	poczekalnie[wybrana_poczekalnia] = 0;
}

// Zwalnia serwis
void zwolnij_serwis(void) {
	serwisy[wybrany_serwis] = 0;
}

// Sekcja lokalna
void sekcja_lokalna(void) {
	switch (stan) {
	case SPRAWNY:
		debug("LS: Pracuję");
		spij(losowa_liczba(MIN_CZEKAJ, MAX_CZEKAJ));
		debug("LS: Zepsułem się");
		stan = ZEPSUTY;
		break;
	case ZEPSUTY:
		debug("LS: Czekam na przyjęcie do poczekalni (zesputy)");
		break;
	case PRZYJMOWANY:
		debug("LS: Czekam na naprawę w poczekalni");
		break;
	case NAPRAWIANY:
		debug("LS: Jestem naprawiany");
		spij(losowa_liczba(MIN_CZEKAJ, MAX_CZEKAJ));
		debug("LS: Zostałem naprawiony");
		stan = NAPRAWIONY;
		break;
	case NAPRAWIONY:
		debug("LS: Czekam na przyjęcie do poczekalni (naprawiony)");
		break;
	case WYDAWANY:
		debug("LS: Czekam na wydanie z poczekalni");
		break;
	}
}

// Sekcja krytyczna
void sekcja_krytyczna(void) {
	switch (stan) {
	case ZEPSUTY:
		if (zarezerwowalem_poczekalnie(WEJSCIE)) {
			debug("CS: Zarezerwowałem poczekalnię");
			stan = PRZYJMOWANY;
		} else {
			debug("CS: Nie udało się zarezerwować poczekalni (zepsuty)");
		}
		break;
	case PRZYJMOWANY:
		if (zarezerwuj_serwis()) {
			zwolnij_poczekalnie();
			debug(
					"CS: Zarezerwowałem serwis i zwolniłem poczekalnię (zesputy)");
			stan = NAPRAWIANY;
		} else {
			debug("CS: Nie udało się zarezerwować serwisu");
		}
		break;
	case NAPRAWIONY:
		if (zarezerwowalem_poczekalnie(WYJSCIE)) {
			zwolnij_serwis();
			debug("CS: Zwolniłem serwis i zarezerwowałem poczekalnię");
			stan = WYDAWANY;
		} else {
			debug("CS: Nie udało się zarezerwować poczekalni (naprawiony)");
		}
		break;
	case WYDAWANY:
		zwolnij_poczekalnie();
		debug("CS: Zwolniłem poczekalnię (naprawiony)");
		stan = SPRAWNY;
		break;
	}
}

// Wywoływane przez głównego lub odbiorcę (wyłącznie)
// Wymaga zamka na mam_zeton
void przeslij_zeton(char *kto) {
	int id_wezla;

	int *zeton_dane = (int *) malloc(
			rozmiar_tablicy_komputerow + rozmiar_tablicy_poczekalni
					+ rozmiar_tablicy_serwisu);
	if (!zeton_dane) {
		error("alloc");
	}

	for (id_wezla = 0; id_wezla < liczba_komputerow; ++id_wezla) {
		if (id_wezla != moj_id) {
			LOCK(&zamek_dostal);
			LOCK(&zamek_zadal);
			// SHARED
			if (zadal[id_wezla] > dostal[id_wezla]) {
				UNLOCK(&zamek_zadal);
				// SHARED
				memcpy(zeton_dane, dostal, rozmiar_tablicy_komputerow);
				UNLOCK(&zamek_dostal);
				memcpy(zeton_dane + liczba_komputerow, poczekalnie,
						rozmiar_tablicy_poczekalni);
				memcpy(zeton_dane + liczba_komputerow + liczba_poczekalni,
						serwisy, rozmiar_tablicy_serwisu);
				// WYSŁANIE ŻETONU
				wyslij(
						MSG_ZETON,
						id_wezla,
						zeton_dane,
						liczba_komputerow + liczba_poczekalni
								+ liczba_serwisow);
				debug("COMM: Wysłałem żeton do %d (%s)", id_wezla, kto);
				// SHARED
				mam_zeton = 0;
				break;
			} else {
				UNLOCK(&zamek_zadal);
				UNLOCK(&zamek_dostal);
			}
		}
	} // for

	free(zeton_dane);
}

// Procedura wątku głównego
void *glowny(void *arg) {
	int nr_wezla, nadawca;
	MPI_Status status;

	int *zeton_dane = (int *) malloc(
			rozmiar_tablicy_komputerow + rozmiar_tablicy_poczekalni
					+ rozmiar_tablicy_serwisu);
	if (!zeton_dane) {
		error("alloc");
	}

	while (!koniec_pracy) {
		sekcja_lokalna();
		LOCK(&zamek_mam_zeton);
		// SHARED
		if (!mam_zeton) {
			moj_num = moj_num + 1;
			for (nr_wezla = 0; nr_wezla < liczba_komputerow; ++nr_wezla) {
				if (nr_wezla != moj_id) {
					// WYSŁANIE ŻĄDANIA
					wyslij(MSG_ZADANIE, nr_wezla, &moj_num, 1);
					debug("COMM: Wysłałem żądanie do %d", nr_wezla);
				}
			}
			// ODBIÓR ŻETONU
			odbierz(MSG_ZETON, zeton_dane,
					liczba_komputerow + liczba_poczekalni + liczba_serwisow,
					&status);
			// SHARED
			MUTEX(&zamek_dostal,
					memcpy(dostal, zeton_dane, rozmiar_tablicy_komputerow));
			memcpy(poczekalnie, zeton_dane + liczba_komputerow,
					rozmiar_tablicy_poczekalni);
			memcpy(serwisy, zeton_dane + liczba_komputerow + liczba_poczekalni,
					rozmiar_tablicy_serwisu);
			nadawca = status.MPI_SOURCE;
			debug("COMM: Odebrałem żeton od %d", nadawca);
			// SHARED
			mam_zeton = 1;
		} // if
		  // SHARED
		MUTEX(&zamek_w_sekcji_krytycznej, w_sekcji_krytycznej = 1);
		UNLOCK(&zamek_mam_zeton);
		sekcja_krytyczna();
		// SHARED
		MUTEX(&zamek_dostal, dostal[moj_id] = moj_num);
		LOCK(&zamek_mam_zeton);
		// SHARED
		MUTEX(&zamek_w_sekcji_krytycznej, w_sekcji_krytycznej = 0);
		// WYSŁANIE ŻETONU
		przeslij_zeton("Główny");
		UNLOCK(&zamek_mam_zeton);
	}

	free(zeton_dane);

	pthread_join(watek_odbiorca, 0);
	debug("Kończę.");
	pthread_exit(0);
	return 0;
}

// Procedura wątku odbiorcy
void *odbiorca(void *arg) {
	int nadawca, numer_zadania;
	MPI_Status status;

	while (!koniec_pracy) {
		// ODBIÓR ŻĄDANIA
		odbierz(MSG_ZADANIE, &numer_zadania, 1, &status);
		nadawca = status.MPI_SOURCE;
		debug("COMM: Odebrałem żądanie od %d", nadawca);
		// SHARED
		MUTEX(&zamek_zadal,
				zadal[nadawca] = max(zadal[nadawca], numer_zadania));
		LOCK(&zamek_mam_zeton);
		LOCK(&zamek_w_sekcji_krytycznej);
		// SHARED
		if (mam_zeton && !w_sekcji_krytycznej) {
			// WYSŁANIE ŻETONU
			przeslij_zeton("Odbiorca");
		} //if
		UNLOCK(&zamek_w_sekcji_krytycznej);
		UNLOCK(&zamek_mam_zeton);
	}

	debug("Kończę.");
	pthread_exit(0);
	return 0;
}

int main(int argc, char *argv[]) {
	int poziom_wspolbieznosci;
	int liczba_procesow;

// Inicjalizacja generatora pseudolosowego
	srand(time(0));
// Buforowanie standardowego wyjścia (Komunikaty mimo wszystko przeplatają się)
	setlinebuf(stdout);
	setlinebuf(stderr);
// Obsługa sygnałów, SIGUSR1 = 10
	(void) signal(SIGUSR1, procedura_zakonczenia);

// Inicjalizacja MPI
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &poziom_wspolbieznosci);
	MPI_Comm_rank(MPI_COMM_WORLD, &moj_id);
	MPI_Comm_size(MPI_COMM_WORLD, &liczba_procesow);
	MPI_Get_processor_name(nazwa_hosta, &dlugosc_nazwy_hosta);

// Utworzenie plików logu
	sprintf(sciezka_logu, "log.%02d", moj_id);
	plik_logu = fopen(sciezka_logu, "w");
	if (plik_logu == 0)
		error("Nie udało się otworzyć pliku logu %s", sciezka_logu);
// Bez buforowania - pozwala ominąć open;expr;close
	setvbuf(plik_logu, (char *) NULL, _IONBF, 0);

// Sprawdzenie argumentów
	if (argc != 4) {
		init_error("Argumenty wywołania: K, N, S.");
	}
	if (!isint(argv[1]) || !isint(argv[2]) || !isint(argv[3])) {
		init_error("Arugmenty muszą być liczbami.");
	}
	liczba_komputerow = atoi(argv[1]);
	liczba_serwisow = atoi(argv[2]);
	liczba_poczekalni = atoi(argv[3]);
	if (liczba_komputerow != liczba_procesow)
		init_error("Nie udało się uruchomić odpowiedniej liczby procesów");
	if (liczba_komputerow < 3 || liczba_serwisow < 2 || liczba_poczekalni < 1
			|| liczba_komputerow > MAX_PROCESY || liczba_serwisow > MAX_PROCESY
			|| liczba_poczekalni > MAX_PROCESY)
		init_error(
				"Musi być spełniony warunek (K >= 3 && N >= 2 && S >= 1) && (K, N, S <= %d).",
				MAX_PROCESY);
	if (!(liczba_komputerow > liczba_serwisow
			&& liczba_serwisow > liczba_poczekalni))
		init_error("Musi być spełniony warunek (K > N > S).");
	rozmiar_tablicy_komputerow = liczba_komputerow * sizeof(int);
	rozmiar_tablicy_poczekalni = liczba_poczekalni * sizeof(int);
	rozmiar_tablicy_serwisu = liczba_serwisow * sizeof(int);
	poczekalnie = (int *) calloc(liczba_poczekalni, sizeof(int));
	serwisy = (int *) calloc(liczba_serwisow, sizeof(int));
	zadal = (int *) calloc(liczba_komputerow, sizeof(int));
	dostal = (int *) calloc(liczba_komputerow, sizeof(int));
	if (!poczekalnie || !serwisy || !dostal || !zadal) {
		error("alloc");
	}

// Inicjalizacja danych węzła
	if (moj_id == 0) {
		debug(
				"Start : K = %d, N = %d, S = %d, MPI thread level = %d",
				liczba_komputerow, liczba_serwisow, liczba_poczekalni, poziom_wspolbieznosci);
		mam_zeton = 1;
	} else {
		debug("Start");
		mam_zeton = 0;
	}
	moj_num = 0;
	w_sekcji_krytycznej = 0;
	stan = SPRAWNY;

// Inicjalizacja wątków
	koniec_pracy = 0;
	int m1, m2, m3, m4, m5;
	int t1, t2;

	m1 = pthread_mutex_init(&zamek_mam_zeton, 0);
	m2 = pthread_mutex_init(&zamek_zadal, 0);
	m3 = pthread_mutex_init(&zamek_dostal, 0);
	m4 = pthread_mutex_init(&zamek_w_sekcji_krytycznej, 0);
// Begin MPI Thread Level 0 Thread Support Hack
	m5 = pthread_mutex_init(&zamek_MPI, 0);
// End MPI Thread Level 0 Thread Support Hack
	if (m1 || m2 || m3 || m4 || m5) {
		error("pthread_mutex_init");
	}
// Utworzenie wątków
	t1 = pthread_create(&watek_glowny, 0, glowny, 0);
	t2 = pthread_create(&watek_odbiorca, 0, odbiorca, 0);
	if (t1 || t2) {
		error("pthread_create");
	}

// Kończenie
	pthread_join(watek_glowny, 0);
	MPI_Finalize();
	fclose(plik_logu);
	free(zadal);
	free(dostal);
	free(poczekalnie);
	free(serwisy);
	pthread_mutex_destroy(&zamek_mam_zeton);
	pthread_mutex_destroy(&zamek_zadal);
	pthread_mutex_destroy(&zamek_dostal);
	pthread_mutex_destroy(&zamek_w_sekcji_krytycznej);
// Begin MPI Thread Level 0 Thread Support Hack
	pthread_mutex_destroy(&zamek_MPI);
// End MPI Thread Level 0 Thread Support Hack
	if (moj_id == 0) {
		debug("Koniec.");
	}
	return 0;
}

