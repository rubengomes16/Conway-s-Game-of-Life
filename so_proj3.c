
/*
Rúben dos Santos Gomes 2010135447
Tiago Filipe Teixeira Sapata 2005107477
Tempo gasto no total: 46 horas total
*/

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>

// PARAMETROS-----------
#define LINHAS 30
#define COLUNAS 30
#define NUM_THREADS 4
#define FREQ_SNAP 10
#define NUM_GERACOES 100
#define NUM_SINGLE 	0
#define NUM_BLOCK 	1
#define NUM_GLIDER 	2
#define NUM_LSS 	2
#define NUM_PULSAR 	1
//----------------------

#define RACIO_F_TAB 0.5
#define MAT(i,j) matriz[(((i)*COLUNAS)+(j))]
#define MAT2(k,i,j) matriz[((k*(LINHAS*COLUNAS))+(((i)*COLUNAS)+(j)))]
#define LADO_SINGLE 3
#define LADO_BLOCK 4
#define LADO_GLIDER 5
#define LADO_LSS 7
#define LADO_PULSAR 15
#define N_MAT 2
#define SIZE_SNAP_FILE 20
#define FILESIZE (2*LINHAS*COLUNAS*sizeof(char))
#define	FILE_MODE	(S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

#define VIVA 88
#define MORTA 45

typedef struct thread_data
{
	int ind_start;
	int ind_end;
	int id;
}ThreadData;


int fillMatriz(char *matriz);
int isFreeSpot(char *matriz, int posi, int posj, int sizeLado);
void putPulsar(char *matriz, int posi, int posj);
void putLSS(char *matriz, int posi, int posj);
void putGlider(char *matriz, int posi, int posj);
void putBlock(char *matriz, int posi, int posj);
void putSingle(char *matriz, int posi, int posj);
void initThData(int n_thread, ThreadData *thData, int linhasPorTh, int linhasPorThResto);
void* worker(void *arg);
void checkParams();
void init();
void printGrid(char *matriz, int nlinhas, int ncolunas, int ind_mat);
int getMinGen();
int processCell(char *matriz, int i, int j);
void processGrid(char *matriz, int startLinha, int endLinha, int ind_mat_write);
void snapshot(char *matriz, int ind_mat);
double tempo();

char *matriz;
int file_count_snap = 0;
pthread_t thread_ids[NUM_THREADS];
ThreadData th_data[NUM_THREADS];
int curr_gen[NUM_THREADS] = {0};	//geracao ja terminada por cada thread
int count_finish = 0;
pthread_cond_t cond_vars[NUM_THREADS];
pthread_mutex_t mutexes[NUM_THREADS];

sem_t sem_count_snap;
sem_t sem_barreira_snap;
sem_t sem_count_snap2;
sem_t sem_barreira_snap2;
int count_snap = 0;

int main()
{
	double t1,t2;
	t1 = tempo();

	checkParams();
	init();
	if (FREQ_SNAP>1)
		snapshot(matriz,0);
	int linhasPorTh = LINHAS/NUM_THREADS;
	int linhasPorThResto = LINHAS%NUM_THREADS;

	int i;
	for (i=0; i<NUM_THREADS; i++) //cria threads
	{
		initThData(i, &th_data[i], linhasPorTh, linhasPorThResto); //inicializa dados a passar a cada thread
		pthread_create(&thread_ids[i], NULL, worker, (void *) &(th_data[i].id));
	}
	
	// espera que threads criadas terminem execução
	for (i=0; i<NUM_THREADS; i++)
		pthread_join(thread_ids[i], NULL);
	
	t2 = tempo();
	printf("\nTemp de processamento - %lf segundos\n\n",t2-t1);
	
	return 0;
}

void* worker(void *arg)
{
	int id = *((int*) arg);
	int start = th_data[id].ind_start;
	int end = th_data[id].ind_end;
	int id_cima = (id+NUM_THREADS-1)%NUM_THREADS;
	int id_baixo = (id+NUM_THREADS+1)%NUM_THREADS;
	
	//printf("[%d] current gen: %d | start: %d | end: %d\n", id, curr_gen[id], start, end);
	//printf("[%d] cima: %d | baixo: %d\n", id, (id+NUM_THREADS-1)%NUM_THREADS, (id+NUM_THREADS+1)%NUM_THREADS);
	
	
	int gen; 		 		//geracao a calcular
	int gen_ind_mat_write;	//indice da matriz correspondente à geração a calcular 
	for (gen = 1; gen<=NUM_GERACOES; gen++) // geracao a calcular
	{	
		
		gen_ind_mat_write = gen%2;
		//processa linhas do meio
		processGrid(matriz, start+1, end-1, gen_ind_mat_write);
		
		//verifica se pode calcular linha de cima
		pthread_mutex_lock(&mutexes[id_cima]);
		while (curr_gen[id_cima] < gen-1)
		{
			pthread_cond_wait(&cond_vars[id_cima], &mutexes[id_cima]);
		}
		pthread_mutex_unlock(&mutexes[id_cima]);
		processGrid(matriz, start, start, gen_ind_mat_write); //processa linha de cima
		
		//verifica se pode calcular linha de baixo
		pthread_mutex_lock(&mutexes[id_baixo]);
		while (curr_gen[id_baixo] < gen-1)
		{
			pthread_cond_wait(&cond_vars[id_baixo], &mutexes[id_baixo]);
		}
		pthread_mutex_unlock(&mutexes[id_baixo]);
		processGrid(matriz, end, end, gen_ind_mat_write);
		
		pthread_mutex_lock(&mutexes[id]);
		curr_gen[id]++;
		pthread_cond_broadcast(&cond_vars[id]);
		pthread_mutex_unlock(&mutexes[id]);
		
		if (gen%FREQ_SNAP == 0)
		{	
			sem_wait(&sem_count_snap);
			count_snap++;
			if (count_snap == NUM_THREADS) // aqui só entra a ultima thread de uma geracao de snapshot
			{
				count_snap = 0;
				snapshot(matriz, 1-gen_ind_mat_write);
				
				//liberta threads da barreira
				int i;
				for (i = 0; i<NUM_THREADS; i++)
					sem_post(&sem_barreira_snap);
			}
			sem_post(&sem_count_snap);
			sem_wait(&sem_barreira_snap);
			
			sem_wait(&sem_count_snap2);
			count_snap++;
			if (count_snap == NUM_THREADS) // aqui só entra a ultima thread de uma geracao de snapshot
			{
				count_snap = 0;
				//liberta threads da 2a barreira
				int i;
				for (i = 0; i<NUM_THREADS; i++)
					sem_post(&sem_barreira_snap2);
			}
			sem_post(&sem_count_snap2);
			sem_wait(&sem_barreira_snap2);
		}
		
	}
	pthread_exit(NULL);
	return NULL;
}

void snapshot(char *matriz, int ind_mat)
{
	matriz += ind_mat*LINHAS*COLUNAS;
    int fd, i, j;
    int result;
    char *map, *temp_map; 
    char ficheiro[SIZE_SNAP_FILE]; 
	file_count_snap++;
    sprintf(ficheiro, "snapshot%d.txt", file_count_snap);
    
    fd = open(ficheiro, O_RDWR | O_CREAT | O_TRUNC, FILE_MODE);
    if (fd == -1) {
	   perror("Erro ao abrir ficheiro para snapshot! A terminar...\n");
	   exit(0);
    }
    
    result = lseek(fd, FILESIZE-1, SEEK_SET);
    if (result == -1) {
    	close(fd);
    	perror("Erro ao chamar lseek() para dimensionar ficheiro para snapshot! A terminar...\n");
    	exit(0);
    }
    
    result = write(fd, "\n", 1);
    if (result != 1) {
	   close(fd);
	   perror("Erro ao escrever ultimo byte do ficheiro");
	   exit(0);
    }

    // mapeamento do ficheiro
    map = (char*)mmap(0, FILESIZE, PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
	   close(fd);
	   perror("Erro ao mapear ficheiro! A terminar...\n");
	   exit(0);
    }
    
    // 
	temp_map=map;
	for( i=0;i<LINHAS;i++){
		for ( j = 0; j < COLUNAS; j++)
		{
			if(j==COLUNAS-1)
				sprintf(temp_map, "%c\n", MAT(i,j));
			else
				sprintf(temp_map, "%c ", MAT(i,j));
			temp_map+=strlen(temp_map);
		}
 	}

    if (munmap(map, FILESIZE) == -1) {
		perror("Erro em unmap do ficheiro de snapshot! A terminar...\n");
    }
    close(fd);
}


void processGrid(char *matriz, int startLinha, int endLinha, int ind_mat_write)
{
	int ind_mat_read = (ind_mat_write+1)%2;
	int i, j, vizinhos;
	
	// printf("Thread starting: linhaStart(%d) linhaEnd(%d)\n", startLinha, endLinha); // TO DEL
	for (i = startLinha; i<=endLinha; i++)
	{
		for (j = 0; j<COLUNAS; j++)
		{
			vizinhos = processCell(matriz+ind_mat_read*(LINHAS*COLUNAS), i, j); //retorna numero de celulas vivas na vizinhança
			if (vizinhos < 2)
				MAT2(ind_mat_write, i, j) = MORTA;
			else if ( vizinhos==2 )
				if (MAT2(ind_mat_read, i, j) == VIVA)
					MAT2(ind_mat_write, i, j) = VIVA;
				else
					MAT2(ind_mat_write, i, j) = MORTA;
			else if ( vizinhos==3 )
				MAT2(ind_mat_write, i, j) = VIVA; //celula viva ou morta com 3 vizinhos vivos vive na proxima geracao
			else // vizinhos > 3
				MAT2(ind_mat_write, i, j) = MORTA; //celula viva ou morta com mais de 3 vizinhos morre na proxima geracao

		}
	}
}

int processCell(char *matriz, int i, int j)
{	
	int nalive = 0;
	if( MAT( (i+LINHAS-1)%LINHAS, (j+COLUNAS-1)%COLUNAS ) == VIVA)
		nalive++;
	if( MAT( (i+LINHAS)%LINHAS, (j+COLUNAS-1)%COLUNAS ) == VIVA)
		nalive++;
	if( MAT( (i+LINHAS+1)%LINHAS, (j+COLUNAS-1)%COLUNAS ) == VIVA)
		nalive++;
	
	if( MAT( (i+LINHAS-1)%LINHAS, (j+COLUNAS)%COLUNAS ) == VIVA)
		nalive++;
	if( MAT( (i+LINHAS+1)%LINHAS, (j+COLUNAS)%COLUNAS ) == VIVA)
		nalive++;
		
	if( MAT( (i+LINHAS-1)%LINHAS, (j+COLUNAS+1)%COLUNAS ) == VIVA)
		nalive++;
	if( MAT( (i+LINHAS)%LINHAS , (j+COLUNAS+1)%COLUNAS ) == VIVA)
		nalive++;
	if( MAT( (i+LINHAS+1)%LINHAS, (j+COLUNAS+1)%COLUNAS ) == VIVA)
		nalive++;
	
	return nalive;
}

int getMinGen()
{
	int i, min = curr_gen[0];
	for (i=1; i<NUM_THREADS; i++)
	{
		if (min>curr_gen[i])
			min = curr_gen[i];
	}
	return min;
}


void printGrid(char *matriz, int nlinhas, int ncolunas, int ind_mat)
{
	if (ind_mat >= N_MAT)
		return;
	//printf("=== PRINTGRID()======\n");
	//matriz += ind_mat*(LINHAS*COLUNAS); //--->subtituido por MAT2(ind_mat,i,k) em baixo
	int i, j;
	for (i = 0; i<nlinhas; i++)
	{
		for (j = 0; j<ncolunas; j++)
			printf("%c ", MAT2(ind_mat,i,j));
		printf("\n");
	}
}

void init()
{
	//alocamento de memoria para as 2 matrizes usadas
	matriz = (char *) malloc(sizeof(char)*N_MAT*LINHAS*COLUNAS);
	if (matriz == NULL)
	{
		printf("Erro ao alocar memoria para matriz. A terminar...\n");
		exit(0);
	}
	
	//preenchimento das 2 matrizes usadas
	memset( matriz, MORTA, sizeof(char)*N_MAT*LINHAS*COLUNAS);
	if (fillMatriz(matriz) == -1)
	{
		printf("Escolha um tabuleiro maior ou reduza num de figuras. A terminar...\n");
		exit(0);
	}
	
	//condition variables e mutexes para cada uma das threads
	int i;
	for (i=0; i<NUM_THREADS; i++)
	{
		pthread_cond_init(&cond_vars[i], NULL);
		pthread_mutex_init(&mutexes[i], NULL);
	}
	
	//semaforos
	sem_init(&sem_count_snap, 0, 1);
	sem_init(&sem_barreira_snap, 0, 0);
	sem_init(&sem_count_snap2, 0, 1);
	sem_init(&sem_barreira_snap2, 0, 0);
}

void checkParams()
{
	if (LINHAS/NUM_THREADS < 3)
	{
		printf("Numero de linhas por thread tem de ser superior a 3! A terminar...\n");
		exit(0);
	}
	
	if ( NUM_PULSAR*LADO_PULSAR*LADO_PULSAR + NUM_LSS*LADO_LSS*LADO_LSS + NUM_GLIDER*LADO_GLIDER*LADO_GLIDER + NUM_BLOCK*LADO_BLOCK*LADO_BLOCK + NUM_SINGLE*LADO_SINGLE*LADO_SINGLE > RACIO_F_TAB*LINHAS*COLUNAS)
	{
		printf("Escolha um tabuleiro maior ou reduza num de figuras. A terminar...\n");
		exit(0);
	}
}

void initThData(int n_thread, ThreadData *thData, int linhasPorTh, int linhasPorThResto)
{
	int startLinha, endLinha;
	if (n_thread==0) // 1a thread a ser criado
	{
		startLinha = 0;
		endLinha = linhasPorTh-1;
	}
	else if (n_thread == NUM_THREADS-1) // ultima thread a ser criada
	{
		startLinha = n_thread*linhasPorTh;
		endLinha = startLinha+linhasPorTh+linhasPorThResto-1;
	}
	else
	{
		startLinha = n_thread*linhasPorTh;
		endLinha = startLinha+linhasPorTh-1;
	}
	
	thData->ind_start = startLinha;
	thData->ind_end = endLinha;
	thData->id = n_thread;
	
}

void putSingle(char *matriz, int posi, int posj)
{
	MAT( (posi+1+LINHAS)%LINHAS, (posj+1+COLUNAS)%COLUNAS ) = VIVA;
}

void putBlock(char *matriz, int posi, int posj)
{
	MAT( (posi+1+LINHAS)%LINHAS, (posj+1+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+1+LINHAS)%LINHAS, (posj+2+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+2+LINHAS)%LINHAS, (posj+1+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+2+LINHAS)%LINHAS, (posj+2+COLUNAS)%COLUNAS ) = VIVA;
}

void putGlider(char *matriz, int posi, int posj)
{
	MAT( (posi+1+LINHAS)%LINHAS, (posj+2+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+2+LINHAS)%LINHAS, (posj+3+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+3+LINHAS)%LINHAS, (posj+1+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+3+LINHAS)%LINHAS, (posj+2+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+3+LINHAS)%LINHAS, (posj+3+COLUNAS)%COLUNAS ) = VIVA;
}

void putLSS(char *matriz, int posi, int posj)
{
	MAT( (posi+1+LINHAS)%LINHAS, (posj+1+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+1+LINHAS)%LINHAS, (posj+4+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+2+LINHAS)%LINHAS, (posj+5+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+3+LINHAS)%LINHAS, (posj+1+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+3+LINHAS)%LINHAS, (posj+5+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+4+LINHAS)%LINHAS, (posj+2+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+4+LINHAS)%LINHAS, (posj+3+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+4+LINHAS)%LINHAS, (posj+4+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+4+LINHAS)%LINHAS, (posj+5+COLUNAS)%COLUNAS ) = VIVA;
}

void putPulsar(char *matriz, int posi, int posj)
{
	MAT( (posi+1+LINHAS)%LINHAS, (posj+3+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+1+LINHAS)%LINHAS, (posj+4+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+1+LINHAS)%LINHAS, (posj+5+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+1+LINHAS)%LINHAS, (posj+9+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+1+LINHAS)%LINHAS, (posj+10+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+1+LINHAS)%LINHAS, (posj+11+COLUNAS)%COLUNAS ) = VIVA;
	
	MAT( (posi+3+LINHAS)%LINHAS, (posj+1+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+3+LINHAS)%LINHAS, (posj+6+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+3+LINHAS)%LINHAS, (posj+8+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+3+LINHAS)%LINHAS, (posj+13+COLUNAS)%COLUNAS ) = VIVA;

	MAT( (posi+4+LINHAS)%LINHAS, (posj+1+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+4+LINHAS)%LINHAS, (posj+6+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+4+LINHAS)%LINHAS, (posj+8+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+4+LINHAS)%LINHAS, (posj+13+COLUNAS)%COLUNAS ) = VIVA;

	MAT( (posi+5+LINHAS)%LINHAS, (posj+1+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+5+LINHAS)%LINHAS, (posj+6+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+5+LINHAS)%LINHAS, (posj+8+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+5+LINHAS)%LINHAS, (posj+13+COLUNAS)%COLUNAS ) = VIVA;

	MAT( (posi+6+LINHAS)%LINHAS, (posj+3+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+6+LINHAS)%LINHAS, (posj+4+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+6+LINHAS)%LINHAS, (posj+5+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+6+LINHAS)%LINHAS, (posj+9+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+6+LINHAS)%LINHAS, (posj+10+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+6+LINHAS)%LINHAS, (posj+11+COLUNAS)%COLUNAS ) = VIVA;

	MAT( (posi+8+LINHAS)%LINHAS, (posj+3+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+8+LINHAS)%LINHAS, (posj+4+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+8+LINHAS)%LINHAS, (posj+5+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+8+LINHAS)%LINHAS, (posj+9+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+8+LINHAS)%LINHAS, (posj+10+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+8+LINHAS)%LINHAS, (posj+11+COLUNAS)%COLUNAS ) = VIVA;

	MAT( (posi+9+LINHAS)%LINHAS, (posj+1+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+9+LINHAS)%LINHAS, (posj+6+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+9+LINHAS)%LINHAS, (posj+8+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+9+LINHAS)%LINHAS, (posj+13+COLUNAS)%COLUNAS ) = VIVA;

	MAT( (posi+10+LINHAS)%LINHAS, (posj+1+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+10+LINHAS)%LINHAS, (posj+6+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+10+LINHAS)%LINHAS, (posj+8+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+10+LINHAS)%LINHAS, (posj+13+COLUNAS)%COLUNAS ) = VIVA;

	MAT( (posi+11+LINHAS)%LINHAS, (posj+1+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+11+LINHAS)%LINHAS, (posj+6+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+11+LINHAS)%LINHAS, (posj+8+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+11+LINHAS)%LINHAS, (posj+13+COLUNAS)%COLUNAS ) = VIVA;

	MAT( (posi+13+LINHAS)%LINHAS, (posj+3+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+13+LINHAS)%LINHAS, (posj+4+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+13+LINHAS)%LINHAS, (posj+5+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+13+LINHAS)%LINHAS, (posj+9+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+13+LINHAS)%LINHAS, (posj+10+COLUNAS)%COLUNAS ) = VIVA;
	MAT( (posi+13+LINHAS)%LINHAS, (posj+11+COLUNAS)%COLUNAS ) = VIVA;
}

int isFreeSpot(char *matriz, int posi, int posj, int sizeLado)
{
	int i, j;
	for (i = 0; i<sizeLado; i++)
	{
		for (j = 0; j<sizeLado; j++)
		{
			if( MAT( (posi+i+LINHAS)%LINHAS, (posj+j+COLUNAS)%COLUNAS ) == VIVA)
				return 0;
		}
	}
	return 1;
}

int fillMatriz(char *matriz)
{
	int figs[] = {NUM_PULSAR, NUM_LSS, NUM_GLIDER, NUM_BLOCK, NUM_SINGLE};
	int lados[] = {LADO_PULSAR, LADO_LSS, LADO_GLIDER, LADO_BLOCK, LADO_SINGLE};
	int n, i, j, count;
	
	
	for (n=0; n<5; n++)
	{
		count = 0;
		while(figs[n] > 0)
		{
			if (count>100)
				return -1;
			else
				count++;
			i = rand()%(LINHAS-lados[n]-1);
			j = rand()%(COLUNAS-lados[n]-1);
			//printf("%d ",i);
			//printf("%d\n",j);
			
			if (isFreeSpot(matriz, i, j, lados[n]))
			{	
				switch (n)
				{
					case 0:
						putPulsar(matriz, i, j);
						break;
					case 1:
						putLSS(matriz, i, j);
						break;
					case 2:
						putGlider(matriz, i, j);
						break;
					case 3:
						putBlock(matriz, i, j);
						break;
					case 4:
						putSingle(matriz, i, j);
						break;
				}
				figs[n]--;
			}
			
		}
	}
	return 0;
}

double tempo()
{
	struct timeval tv;
	gettimeofday(&tv,0);
	return tv.tv_sec + tv.tv_usec/1e6;
}





















