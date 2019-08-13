#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define numnodes 59
#define minnodes 48
#define freq 5
#define minsigs 13
#define startht 235
#define endht 200000
#define recvmask 0x5effffffeffff9d
#define DPOW_MODIND(ht,offset,freq) ((((ht / freq) % numnodes) + (offset)) % numnodes)

#define RESET   "\033[0m"
#define BLACK   "\033[30m"      /* Black */
#define RED     "\033[31m"      /* Red */
#define GREEN   "\033[32m"      /* Green */
#define YELLOW  "\033[33m"      /* Yellow */
#define BLUE    "\033[34m"      /* Blue */
#define MAGENTA "\033[35m"      /* Magenta */
#define CYAN    "\033[36m"      /* Cyan */
#define WHITE   "\033[37m"      /* White */
#define BOLDBLACK   "\033[1m\033[30m"      /* Bold Black */
#define BOLDRED     "\033[1m\033[31m"      /* Bold Red */
#define BOLDGREEN   "\033[1m\033[32m"      /* Bold Green */
#define BOLDYELLOW  "\033[1m\033[33m"      /* Bold Yellow */
#define BOLDBLUE    "\033[1m\033[34m"      /* Bold Blue */
#define BOLDMAGENTA "\033[1m\033[35m"      /* Bold Magenta */
#define BOLDCYAN    "\033[1m\033[36m"      /* Bold Cyan */
#define BOLDWHITE   "\033[1m\033[37m"      /* Bold White */

uint32_t nodes[64]={0};
uint8_t indexes[64]={0};

int32_t bitweight(uint64_t x)
{
    int i,wt = 0;
    for (i=0; i<64; i++)
        if ( (1LL << i) & x )
            wt++;
    return(wt);
}

uint64_t bestk(int32_t ht, int8_t *lastkp)
{
    int32_t j,m,k,z,n,p,i; uint64_t bestmask,mask = 0;//bp->require0;
    //uint8_t badnodes[64]={0};
    bestmask = 0;
    *lastkp = -1;
    uint8_t rndnodes[32];
    for ( m=0; m<32; m++ )
        rndnodes[m] = rand() % numnodes;
    
    m = 0;//bp->require0;
    for (z=n=0; z<numnodes; z++)
        if ( bitweight(recvmask) >= minnodes )
            n++;
    if ( n < minnodes )
        return(bestmask);
    for (j=0; j<numnodes; j++)
    {
        //if ( (recvmask & (1LL << j)) == 0 ) 
        //    continue;
        k = i = DPOW_MODIND(ht,j,freq);
        for ( p=0; p<32; p++ ) 
        {
            if ( (recvmask & (1LL << k)) != 0 )
                break;
            k += rndnodes[(k>>1)]+p;
            while ( k >= numnodes ) 
                k -= numnodes;
            fprintf(stderr, CYAN">>>>>>> p.%i k.%i vs newk.%i inrecv.%i \n"RESET, p, i, k, ((recvmask & (1LL << k)) != 0));
            indexes[k]++;
        }
        if ( (recvmask & (1LL << k)) == 0 ) 
            continue;
        mask |= (1LL << k);
        if ( ++m == minsigs )
        {
            *lastkp = k;
            bestmask = mask;
            for ( p=0; p<numnodes; p++ )
            {
                if ( (bestmask & (1LL << p)) != 0 )
                {
                    nodes[p]++;
                    fprintf(stderr, "%i, ",p);
                }
            }
            printf(" ht.%i bestk.%d %llx \n",ht,*lastkp,(long long)bestmask);
        }
    }
    
    /*for ( p=0; p<numnodes; p++ )
    {
        if ( (bestmask & (1LL << p)) != 0 )
        {
            if ( (recvmask & (1LL << p)) != 0 ) 
            {
                nodes[p]++;
                //printf("%i, ",p);
            }
            else 
            {
                badnodes[p]++;
                int32_t rndnode = *lastkp;
                if ( rndnode >= numnodes )
                    rndnode = rndnode-numnodes;
                
                for ( i=0; i<numnodes; i++)
                {
                    fprintf(stderr, "rndnode.%i\n", rndnode);
                    if ( rndnode >= numnodes )
                        rndnode = rndnode-numnodes;;
                    rndnode += i; //(ht % numnodes);
                    if ( (recvmask & (1LL << rndnode)) != 0 && (bestmask & (1LL << rndnode)) == 0 )
                    {
                        bestmask &= ~(1LL << p);
                        bestmask |= (1LL << rndnode);
                        fprintf(stderr,RED"offlinenode.%i -> %i "RESET, j, rndnode);
                        nodes[rndnode]++;
                        indexes[rndnode]++;
                        break;
                    }
                }
            }
        }
    } */
    
    
    //int32_t randperm[59] = { 29,13,42,57,15,35,50,21,28,33,5,18,47,49,19,41,1,6,25,0,32,10,20,39,46,3,7,14,26,36,38,48,17,34,27,31,58,8,37,22,55,54,45,56,52,51,2,4,23,24,40,16,30,53,9,43,11,44,12};
    // replace offline nodes with online nodes. 
    
    //if ( i > 0 )
    //    printf("newbestmask.%llx\n", (long long)bestmask);
    return(bestmask);
}

int main()
{
    int32_t i, rotations=0; int8_t lastkp;
    for ( i=startht; i<endht; i++ ) 
    {
        if ( i % freq == 0 )
        {
            bestk(i, &lastkp);
            rotations += (lastkp == 0);
            if ( rotations == 2000 )
                break;
        }
    }
    for (i=0; i<numnodes; i++) 
    {
        printf("k.%i total.%i\n",i, indexes[i]);
    }
    for (int p=0; p<numnodes; p++ )
        fprintf(stderr, "node.%i count.%i \n",p,nodes[p]);
    return(0);
}
