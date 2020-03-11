#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef unsigned char BYTE;
typedef unsigned short WORD;

#define NPOS 10
#define NCOLS 9
#define NROWS 9
#define NCELLS 81
#define MAXSTAGES 1024

#define STAGE_COMPLETE 0
#define STAGE_INCOMPLETE 1
#define STAGE_INVALID 2

typedef struct
{
  BYTE n;     // valore definito della cella, se 0 non definito
  BYTE tot;   // numero totale di valori probabili mancanti (da 9 a 1)
  WORD prob;  // bitfield dei valori probabili 0=possibile, 1=escluso 
} CELL;

typedef struct
{
  CELL cells[NCELLS];
  void* next;
} STAGE;

typedef struct
{
  STAGE* head;
  STAGE* tail;
  int size;
} FIFO;


static STAGE* stages[MAXSTAGES];

void STAGE_free(STAGE* s);

void FIFO_reset(FIFO* f)
{
  memset(f, 0, sizeof(FIFO));
}

int FIFO_push(FIFO* f, STAGE* s)
{
  if (f->tail)
    f->tail->next = s;
  else
    f->head = s;
  f->tail = s;
  s->next = NULL;
  return ++f->size;
}

int FIFO_isempty(FIFO* f)
{
  if (f->size>0)
		return 0;
	return 1;
}

STAGE* FIFO_pop(FIFO* f)
{
  STAGE* s = NULL;
  if (f->head)
  {
    s = f->head;
    f->head = (STAGE*)s->next;
		if (f->head==NULL)
			f->tail = NULL;
		f->size--;

  }
  return s;
}

STAGE* FIFO_head(FIFO* f)
{
  return f->head;
}

STAGE* FIFO_tail(FIFO* f)
{
	return f->tail;
}

void FIFO_free(FIFO* f)
{
	while (!FIFO_isempty(f))
		STAGE_free(FIFO_pop(f));
}

STAGE* STAGE_dup(STAGE* s)
{
  STAGE* ns = (STAGE*)calloc(1, sizeof(STAGE));
  memcpy(ns, s, sizeof(STAGE));
  ns->next = NULL;
  return ns;
}

void STAGE_free(STAGE* s)
{
  if (s)
    free(s);
}

void STAGE_dump(STAGE* s)
{
  for (int r = 0; r < NROWS; r++)
  {
    if ((r % 3) == 0)
      printf("\n|---|---|---\n");
    else
      printf("\n");
    for (int c = 0; c < NCOLS; c++)
    {
      if ((c % 3) == 0)
        printf("|");
       printf("%c", '0' + s->cells[(r*NCOLS) + c].n);
    }
  }
  printf("\n|---|---|---\n\n");
}

int STAGES_load(const char* filename)
{
  int count = 0;
  memset(stages, 0, sizeof(stages));
  char b;
  FILE* stream = fopen(filename, "rb");
  if (stream != NULL)
  {
    STAGE stage;
    memset(&stage, 0, sizeof(stage));
    int pos = 0;
    while (1)
    {
      size_t c = fread(&b, sizeof(b), 1, stream);
      if (c == 1)
      {
        if ((b == ' ') || (b == '.') || (b == '0'))
        {
          stage.cells[pos].n = 0;
          stage.cells[pos].tot = 9;
          stage.cells[pos].prob = (WORD)0xffff;
          pos++;
        }
        else if ((b >= '1') && (b <= '9'))
          stage.cells[pos++].n = (BYTE)(b - '0');
        if (pos == NCELLS)
        {
          stages[count++] = STAGE_dup(&stage);
          pos = 0;
        }
      }
      else
        break;
    }
    fclose(stream);
  }
  return count;
}

void STAGES_unload(void)
{
  for (int i = 0; i < MAXSTAGES; i++)
  {
    if (stages[i])
      free(stages[i]);
    else
      break;
  }
}

int STAGE_check(STAGE* s)
{
  int c, r, bc, br;
  int valid = 1;
  int ret = STAGE_COMPLETE;
  int nums[10];
  // controlla le righe
  for (r=0; (r<NROWS)&&(valid); r++)
  {
    memset(nums, 0, sizeof(nums));
    for (c=0; (c<NCOLS)&&(valid); c++)
    {
      BYTE n = s->cells[(r*NCOLS)+c].n;
      if (n)
      {
        if (nums[n])
          valid = 0;
        else
          nums[n] = 1;
      }
      else
        ret = STAGE_INCOMPLETE;
    }
  }
  // controlla le colonne
  for (c=0; (c<NCOLS)&&(valid); c++)
  {
    memset(nums, 0, sizeof(nums));
    for (r=0; (r<NROWS)&&(valid); r++)
    {
      BYTE n = s->cells[(r*NCOLS)+c].n;
      if (n)
      {
        if (nums[n])
          valid = 0;
        else
          nums[n] = 1;
      }
      else
        ret = STAGE_INCOMPLETE;
    }
  }

  // controlla i blocchi
  for (br=0; (br<NROWS)&&(valid); br += 3)
  {
    for (bc=0; (bc<NCOLS)&&(valid); bc += 3)
    {
      memset(nums, 0, sizeof(nums));
      for (r=br; (r<(br+3))&&(valid); r++)
      {
        for (c=bc; (c<(bc+3))&&(valid); c++)
        {
          BYTE n = s->cells[(r*NCOLS)+c].n;
          if (n)
          {
            if (nums[n])
              valid = 0;
            else
              nums[n] = 1;
          }
          else
            ret = STAGE_INCOMPLETE;
        }
      }
    }
  }

  if (!valid)
    ret = STAGE_INVALID;
  return ret;
}

static int mask_prob(STAGE* s, int pos, BYTE v)
{
  int modified = 0;
  WORD bit = (1 << (WORD)(v-1));
  if (s->cells[pos].prob&bit)
  {
    modified = 1;
    s->cells[pos].prob &= ~bit;
    s->cells[pos].tot--;
    if (s->cells[pos].tot==1)
    {
      for (WORD i=0; i<9; i++)
      {
        if (s->cells[pos].prob&(1 << i))
        {
          s->cells[pos].n = (BYTE)(i+1);
          break;
        }
      }
    }
  }
  return modified;
}


static int mask_bits(STAGE* s, int pos, WORD bits)
{
  WORD i;
	int modified = 0;
	for (i=0; i<9; i++)
	{
		if (bits&(1<<i))
		{
			if (s->cells[pos].prob&(1<<i))
			{
				modified = 1;
				s->cells[pos].prob &= ~(1<<i);
				s->cells[pos].tot--;		
			}
		}
	}
	if ((modified)&&(s->cells[pos].tot==1))
	{
		for (i=0; i<9; i++)
		{
			if (s->cells[pos].prob&(1<<i))
			{
				s->cells[pos].n = (BYTE)(i+1);
				break;
			}
		}
	}
	return modified;
}

int STAGE_process(STAGE* s)
{
  int modified = 0;
  int r, c, cc, rr, br, bc, pp;
  // applica le regole A, B ed eventualmente C
  // se modifica qualcosa, torna 1, altrimenti 0
  // per tutte le colonne
	// regola A: caselle non definite
  int pos = 0;
  while (pos<NCELLS)
  {
    if (s->cells[pos].n==0)
    {
      c = pos % NCOLS;
      r = pos / NCOLS;
      // colonne
      for (cc=0; cc<NCOLS; cc++)
      {
        if (cc!=c)
        {
          BYTE v = s->cells[(r*NCOLS)+cc].n;
          if (v)
					{
            if (mask_prob(s, pos, v))
							modified = 1;
					}
        }
      }
      // righe
			for (rr=0; rr<NROWS; rr++)
      {
        if (rr!=r)
        {
          BYTE v = s->cells[(rr*NCOLS)+c].n;
          if (v)
					{
            if (mask_prob(s, pos, v))
							modified = 1;
					}
        }
      }
			// blocco
			bc = (c/3)*3;
			br = (r/3)*3;
			for (rr=br; rr<(br+3); rr++)
      {
        for (cc=bc; cc<(bc+3); cc++)
        {
          if ((rr==r)&&(cc==c))
						continue;
					BYTE v = s->cells[(rr*NCOLS)+cc].n;
          if (v)
					{
            if (mask_prob(s, pos, v))
							modified = 1;
					}
        }
      }
    }
    pos++;
  }
  // regola B: caselle definite
  pos = 0;
  while (pos<NCELLS)
  {
    if (s->cells[pos].n)
    {
      int n = s->cells[pos].n;
			c = pos % NCOLS;
      r = pos / NCOLS;
      // colonne
      for (cc=0; cc<NCOLS; cc++)
      {
        pp = (r*NCOLS)+cc;
				if (s->cells[pp].n==0)
        {
          if (mask_prob(s, pp, n))
						modified = 1;
			  }
      }
      // righe
			for (rr=0; rr<NROWS; rr++)
      {
        pp = (rr*NCOLS)+c;  
				BYTE v = s->cells[pp].n;
        if (v)
				{
          if (mask_prob(s, pp, v))
						modified = 1;
				}
      }
			// blocco
			bc = (c/3)*3;
			br = (r/3)*3;
			for (rr=br; rr<(br+3); rr++)
      {
        for (cc=bc; cc<(bc+3); cc++)
        {
          if ((rr==r)&&(cc==c))
						continue;
					pp = (rr*NCOLS)+cc;  
					BYTE v = s->cells[pp].n;
          if (v)
					{
            if (mask_prob(s, pp, v))
							modified = 1;
					}
        }
      }
    }
    pos++;
  }

  if ((0)&&(!modified))
  {
    CELL cells[9]; 
		// regola C: esclusione di coppie (tutti quelli che hanno tot==2 nello stesso blocco e sono uguali sulla stessa riga o colonna)
		for (br=0; br<NROWS; br+=3)
		{
			for (bc=0; bc<NCOLS; bc+=3)
			{
				pos=0;
				for (r=br; r<(br+3); r++)
				{
					for (c=bc; c<(bc+3); c++)
					{
						cells[pos++] = s->cells[(r*NCOLS)+c];
					}
				}
				int i=0;
				while (i<8)
				{
					if ((cells[i].tot==2)&&(cells[i].n==0))
					{
						int j=i+1;
						while (j<9)
						{
							if ((cells[j].tot==2)&&(cells[j].n==0)&&(cells[j].prob==cells[i].prob))
							{
								int pos1=((br+(i/3))*NCOLS)+(bc+(i%3));
								int pos2=((br+(j/3))*NCOLS)+(bc+(j%3));
								// verifica se sono sulla stessa colonna o stessa riga
								if ((i/3)==(j/3))
								{
									//stessa riga
									int row = br+(i/3);
									for (int x=0; x<NCOLS; x++)
									{
										int pp=(row*NCOLS)+x;
										if ((pp!=pos1)&&(pp!=pos2))
										{
											if (mask_bits(s, pp, cells[i].prob))
												modified = 1;
										}
									}
								}
								else if ((i%3)==(j%3))
								{
									// stessa colonna
									int col = bc+(i%3);
									for (int x=0; x<NROWS; x++)
									{
										int pp=(x*NCOLS)+col;
										if ((pp!=pos1)&&(pp!=pos2))
										{
											if (mask_bits(s, pp, cells[i].prob))
												modified = 1;
										}
									}
								}
								break;
							}
							j++;
						}
						i=j+1;
					}
					else
						i++;
				}
			}
		}
  }
  return modified;
}

int STAGE_expand(STAGE* s, FIFO* f)
{
  int count = 0;
  // espande la prima casella indenterminata 
  // e mette le espansioni nella fifo
  // ritorna il numero di espansioni
  int pos = 0;
  while (pos < NCELLS)
  {
    if (s->cells[pos].n == 0)
    {
      for (BYTE i = 0; i < 9; i++)
      {
        if (s->cells[pos].prob&(1 << i))
        {
          STAGE* stage = STAGE_dup(s);
          stage->cells[pos].n = i + 1;
          stage->cells[pos].tot--;
          stage->cells[pos].prob &= ~(1 << i);
          FIFO_push(f, stage);
          count++;
        }
      }
      break;
    }
    pos++;
  }
  return count;
}

int main(int argc, char* argv[])
{
  if (argc<2)
	{
		printf("usage: sudoku filename\n");
		return 0;
	}
	
	int completed =0;
	int modified;
  int check = STAGE_INCOMPLETE;
  FIFO fifo;
  FIFO_reset(&fifo);
  int tot = STAGES_load(argv[1]);
  //STAGE_dump(stages[2]);
  for (int i = 0; i < tot; i++)
  {
    if (i==9)
		{
			int b=0;
		}
		STAGE* stage = STAGE_dup(stages[i]);
		printf("STAGE:\n");
		STAGE_dump(stage);
    FIFO_push(&fifo, stage);
    while (!FIFO_isempty(&fifo))
    {
      stage = FIFO_head(&fifo);
      modified = STAGE_process(stage);
			//STAGE_dump(stage);
      check = STAGE_check(stage);
      if (check==STAGE_COMPLETE)
			{
        completed++;
				printf("COMPLETED:\n");
				STAGE_dump(stage);
				FIFO_free(&fifo);
				break;
			}
      else if (check==STAGE_INVALID)
      {
        FIFO_pop(&fifo);
				if (FIFO_isempty(&fifo))
				{
					printf("INVALID: %d\n", i);
				}
				STAGE_free(stage);
			}
      else if (modified)
        continue;
      else
      {
        FIFO_pop(&fifo);
        STAGE_expand(stage, &fifo);
        STAGE_free(stage);
      }
    }
  }
	printf("\nprocessed: %d, completed: %d\n", tot, completed);
  STAGES_unload();
  return 0;
}
