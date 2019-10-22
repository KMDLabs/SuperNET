# New iguana consensus algo by blackjok3r 

We seem to have a problem currently where notarizations are being distributed amoung nodes who respond the fastest rather than a proper rotating distribution that rewards uptime, node reliability, updating and adding coins on time (being a good operator :D ). This has been an extremly long investigation over more than 18 months, by myself and probably used up 1000H of my time at a conservative estimate. I hope this document makes sense and we can all agree on the best path forward, or improve on my findings and solutions even more.

Some common used terms that you need to know: 
- recvmask : This is a bitwise mask, that can have either 1 or 0 for each of the 64 nodes. To enter it (1), you must send utxos to every node, who will then include you in their recvmask otherwise its (0) for your node. 
- bestk : This is a rotating number based on block height that determines the last node elegible for a notarization. eg bestk = 0, numnotaries = 64, minsigs = 13, nodes 52,53,54,55,56,57,58,59,60,61,62,63,0 are chosen to notarize. 
- bestmask: This is the nodes actually chosen to notarize. eg bestk = 0, numnotaries = 64, minsigs = 13, nodes not in recvmask = 54,60, nodes 52,53,55,56,57,58,59,61,62,63,0,1,2 are chosen to notarize. 

## How dPoW works currently:

It may be wise to take a step back and explain how the dpow is actually working currently, it may be easier to see the problem and understand my proposed solution.
    
1) A block happens on the source chain that is (height % freq == 0) so on LABS this is every 5 blocks. This triggers the iguanas to start a `dpow_statemachinestart` thread. This thread runs on a 30s iteration timer. So it should take a few iterations to create a notarization. Obviously faster is better, but in a global distributed network, it's not possible that all nodes can get their message to everyone else instantly, the speed of light exists! :)
    
2) First iteration:
    - Get source/dest chaintips, MoM, MoMoM etc
    - Get utxos for source/dest
    - Send this data to every single node you are connected to: `dpow_send(myinfo,dp,bp,srchash,bp->hashmsg,0,bp->height,(void *)"ping",0);`
    - Receive packets from other nodes: `dpow_nanomsg_update(myinfo);`
    
NOTE: The `dpow_nanomsg_update` function is called periodically on a timer and by all active dpow threads for all coins.
    
3) As the packets are received, `dpow_nanomsg_update` sorts them to the correct coin and dpow_block they belong to. `dpow_nanoutxoget` is the function that deals with the incoming data from all the other nodes. This function calls `dpow_notarize_update` which is responsible for building the recvmask and bestmask, and proceeding to signing stage if possible.
    - `dpow_notarize_update`: Accept packets from nodes that submit utxos, adds these nodes to recvmask, and then calls `dpow_bestconsensus` to see if it has a possible bestk/mask.
    - `dpow_bestconsensus`: Converges nodes who are not in agreement with the majority to the majority. Sets the bp->bestk, bp->recvmask, bp->bestmask. This decides who notarizes the round.  
    - `dpow_notarize_update`: Sends your best consensus values to all nodes "forcefully" with `dpow_send(myinfo,dp,bp,srchash,bp->hashmsg,0,bp->height,(void *)"ping",0);`
        
We can see from the above, that `dpow_send(myinfo,dp,bp,srchash,bp->hashmsg,0,bp->height,(void *)"ping",0);` is called on the receive of packets once a best consensus is known, and this algo favours nodes who respond as fast as possible.
    
## My solution

### RECVMASK

Firstly, to enter the recvmask you must start a dpow thread at the same time as most of the other nodes.

KMD prevDESTHEIGHT logic works ok for what it was designed to do, that is slow down notarizations on AC to every 10 KMD blocks. Because it is a local value that is saved into a global each time a dpow round ends, successful or not, until you are lucky enough to join the online nodes "club" that has the correct KMD height saved, you will never start a dpow round at the right time and never notarize. This is why uptime on nodes is so important, once you get the heights lined up things work great. Also seems to be the reason having loads of KMD peers can help. 

I changed dpow to use blocknotify instead of a loop polling all the daemons for current chaintip, so that all nodes trigger each `dpow_statemachinestart` thread as close together as possible. Thanks CHMEX for the suggestion! 

Blocknotify is not perfect, I have seen some nodes miss blocks, and thus never get to notarize a few times, but it is rare. Decker suggested ZMQ, which may be a cleaner solution, but blocknotify is much more accurate than polling and it was easy to get working with bash scripts/curl.
    
Each `dpow_statemachinestart` is a seperate thread, and we can launch 32 of these at once for each coin, in this version with `DPOW_MAX_BLOCKS` = 32. Normally it never goes past 2 active threads at once and generally its on 0 or 1 active thread per coin.

I changed the state machine thread so it will continue to run until the round has finished. Even with no utxo your node will still wait and converge on the recvmask, bestk and bestmask. This means that in `dpow_sigscheck`, we have a place that we can update state on all nodes, when a notarization has been successfully completed, even if we are not in recvmask for this round.

We save the current KMD height when state machine thread has loaded directly after it has done `dpow_getchaintip`. This gives us a reasonably accurate number on all nodes at the start of each dpow round.
    
When you first start a node, it does not have the prevDESTHEIGHT and so will continue launching state machine threads for every (freq % height == 0) block. This means it will eventually pick one that the rest of the nodes do, and it will then have the correct KMD height once that round has been completed, even if it does not enter recvmask. There is also the previous desttxid which is fetched from `dpow_calcMoM` on thread start, this may not be correct as the last notarization may not be confirmed yet. In this case your node will keep its thread running and wait for the notarization to complete and get the txid when its broadcast on the dest chain. This now means that after a restart, just a max of 2 notarizations are needed to happen on each coin and your node has all the correct info and is in full consensus with the rest of the nodes, and should be starting dpow rounds within a few seconds of all other nodes from now on. 

My solution makes all nodes wait until the first iteration of dpow loop has finished before they move to calculating the bestk/bestmask. This gives ample time for all nodes to make their commitment (be in recvmask, by having valid utxos), this does not really have any protection from nodes making a false commitment (submitting garbage utxo data, invalid sigs, or spent utxo). With `iguana_fastnotariescount` in `iguana_sign.c` we can check notarization transactions (for sapling vin utxos only at this time) to identify exactly which node is the cause of bad sigs. There is also a possibility for a node to submit a spent utxo, these can also be detected by using gettxout on all vins directly after the tx failed to send, although it's not as accurate as the sig check, due to timing issues on a large network. We can implement banning, I have added an example of how to do this but it needs to be properly tested, it may be enough to just have a warning or alert when a node is behaving badly.
    
Because we have all nodes who are online now staying connected for the entire dpow round duration, we know the recvmask for every single node after the first 30s iteration has past. This allows us to simply count the number of nodes in each nodes recvmask, and make sure some minimum agreed recvmask is met by all nodes. This does not mean that each nodes recvmask is exactly the same, just that it can see minimum nodes nodes in its recvmask.
    
```C
for (z=n=0; z<bp->numnotaries; z++)
    if ( bitweight(bp->notaries[z].recvmask) >= bp->minnodes )
        n++;
if ( n < bp->minnodes )
    return(bestmask);
```

If we have the same recvmask on most nodes, its is quite likely that the next round that starts will also have this number of nodes. My solution is just to use the number of nodes in the lastrecvmask directly without any complicated math, or needing many past rounds data points:
    
```C
bp->minnodes = bitweight(dp->lastrecvmask)-1;
```

Most of the time, after the first iteration has completed this number has been met, and by the second iteration (~61-62s after bp->starttime), it is always met, unless nodes go offline between rounds. Each iteration the minimum nodes drops by approximately 1/8 of the total nodes. I have shut down 2 entire regions (24/54 nodes) on the testnet, and notarizations just kept on happening as normal. There was no failed rounds or delays.
     
These changes are iguana consensus affecting, so OPs should not be able to change anything to make their iguana go faster, like I did with this iguana early on. Making things as fast as possible is not always the best idea, it's much better to have it work a little slower, be easy to use and more reliable all at the same time. Any node changing things will only ruin their own consensus, and thus hurt their own counts and node reliability. If you try to send a bestmask/bestk before the required recvmask has been received on all other nodes, they will just ignore you until they see the correct min recvmask (or the min recvmask has lowered enough from the dpow iterations), and by that time, all other nodes have waited, and will have a different bestmask than the one you tried to rush though, killing your chance at making consensus.

### BESTMASK

The bestk/mask is generated in `dpow_maskmin` according to the following macro:  
    
```C
#define DPOW_MODIND(bp,offset,freq) (((((bp)->height / freq) % (bp)->numnotaries) + (offset)) % (bp)->numnotaries)`
```
    
The `DPOW_MODIND itself` is fine, if every node is always online it achieves perfect distribution. Except for these issues:
- Nodes only choose other nodes in their recvmask and this presents a problem because not all nodes have the ability to respond to all nodes in 10ms. Those nodes that can do this, all agree very quickly and move forward to the signing stage before the rest of the nodes even know what's going on. Nodes cannot change their mind either, so once the recvmask is actually updated there are bestmasks that were discarded as invalid that are now valid, but no node cares about this and just sticks with the first bestmask that it saw. The nodes should be making their decision based on the largest data set available not the smallest!
- A node who is offline blocks a bestk, of the corresponding node index from being chosen. eg, Notaries_elected[4] is offline bestk of 4 cannot be chosen.
        
To solve these problems we need to change how the bestk is chosen. For quite a few days, there was one thing I could not get out of my head, and that was just let the bestk rotate as it should, and when it gets to an offline node just select another. This seems simple and safe enough as long as we check this after the first iteration (agreed recvmasks), most nodes will agree and then converge the rest of them to the consensus result using the largest data set available rather than the smallest.
    
The problem is, what data point do you use to select a node to replace a dead one...
If it was a fixed value, then any specific node being down would send its "spot" to either one or a small hand full of nodes each time. And it cannot randomise, with `rand()` or increment with a static variable, because all nodes need to have the same number. We need something that all nodes can agree on that is also random enough that it distributes the "free notarizations" as evenly as possible between all online notaries who commited to the round.

`dp->prevnotatxid` 

Is the txid of the previous notarization on the destination chain, for example for LABS this is the KMD txid, shown as `notarizedtxid` in `komodo-cli -ac_name=LABS getinfo`

We fetch this at the start of a dpow thread, only if it has not been set previously (for iguana launch). It is also updated in `dpow_sigscheck` upon a completed notarization, in case we make a new notarization before the last one has been confirmed on the chain, if it was not confirmed yet, then 2 notarizations in a row could use the same seed. 

```C
uint8_t rndnodes[32];
printf(BLUE"Random seed: ");
for ( i=0; i<32; i++ )
{
    rndnodes[i] = (dp->prevnotatxid.bytes[i] % (bp->numnotaries-1))+1;
    printf("%i ", rndnodes[i]);
}
```

`Random seed: 7 7 6 18 39 20 18 1 40 34 30 30 2 3 3 1 39 47 11 34 13 16 2 20 4 35 7 52 46 19 20 51`

These numbers then need to be fed into some deterministic function that will return the exact same values on most nodes, so that a new bestmask can be converged on, rather than needing to choose a new bestk.

```C
for (j=0; j<bp->numnotaries; j++)
{
    jk = ((k=i= DPOW_MODIND(bp,j,dp->freq))>>1);
    for ( p=0; p<32; p++ )
    {
        if ( (bp->recvmask & (1LL << k)) != 0 && (mask & (1LL << k)) == 0 )
            break;
        jk = ((jk >= 32) ? 0 : jk+1);
        k += rndnodes[jk];
        while ( k >= bp->numnotaries )
            k -= bp->numnotaries;
    }
    if ( p == 32 )
        continue;
    mask |= (1LL << k);
    if ( ++m == bp->minsigs )
    {
        lastkp = i;
        bestmask = mask;
        z = k;
    }
}
```

Num notaries can be a maximum of 64, so there is half of this value random node numbers we can use. The txid is signed by all notaries, so I don't think any individual or group can change it to give themselves any advantage, but maybe I missed something here.

We take the k value from `DPOW_MODIND`, we halve it to get an index of the rndnodes array (jk), this gives one number, but we can then iterate along the array getting different numbers each time. With some simple subtraction, we can make sure the result is always in the range required. The simulation came out within less than 1-2% variance with almost any number of nodes removed from the recvmask.

All that is then required is to make sure the node chosen is not already in the bestmask, and is also in recvmask. The recvmask was being calculated inside this very function itself before, so that had to be sent somewhere earlier on in the process. This either was already like this or my simple change to `dpow_notarize_update` did the trick I'm not totally sure, but it works.

This now gives what I thought at first was simply an impossible result:

`new PENDING BESTK (9 50000082101039d) state.0 last 64 bestks: 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 39, 38, 36, 35, 33, 32, 31, 30, 28, 27, 26, 25, 24, 22, 21, 20, 18, 17, 16, 15, 14, 13, 11, 9, 8, 7, 5, 2, 1, 0,`

We can see that this is not a perfect +1 all the way, but gaps happen because an elegible block height was skipped for some reason, over time I assume it will average out for all bestks, if not then block height will not be a reliable input, and we must think of something else or remove the KMD suppress logic totally so that there is 100% chance to notarize each eligible block, still does not mean 100% chance success rate, but does prevent suppress KMD height skipping blocks.

This finally allows the bestk to rotate as normal, but any node who may happen to be offline inside the chosen bestk, will simply be substituted with a node in recvmask. No longer will an offline node cause an entire bestk group to be skipped, giving a disadvantage to other online nodes in this bestk group.

Example 1 : Old
```
numnotaries = 64, 
minsigs = 13
bestk = 0: 52,53,54,55,56,57,58,59,60,61,62,63,0 are chosen to notarize. 
nodes not in recvmask = 54,60
bestmask: 52,53,55,56,57,58,59,61,62,63,0,1,2 are chosen to notarize. 
```
New: 
```
numnotaries = 64, 
minsigs = 13
bestk = 0: 52,53,54,55,56,57,58,59,60,61,62,63,0 are chosen to notarize. 
nodes not in recvmask = 54,60
bestmask: 52,53,10,55,56,57,58,59,7,61,62,63,0 are chosen to notarize. 
```

Example 2 : Old
```
numnotaries = 64, 
minsigs = 13
bestk = 3: 55,56,57,58,59,60,61,62,63,0,1,2,3 are chosen to notarize. 
nodes not in recvmask = 3, 25
3 offline so new bestk = 4
bestmask: 55,56,57,58,59,61,62,63,0,1,2,4 are chosen to notarize. 
```
New: 
```
numnotaries = 64, 
minsigs = 13
bestk = 3: 55,56,57,58,59,60,61,62,63,0,1,2,3 are chosen to notarize. 
nodes not in recvmask = 3, 25
bestmask: 55,56,57,58,59,60,61,62,63,0,1,2,28 are chosen to notarize. 
```

I hope this made sense, and welcome any suggestions to improve it! 

### UTXO Cache

In my forks of BTC16.3 and Komodo/dev I have implemented a PoC utxo cache. Which filters all the 10ksat utxos iguana needs and saves them in a vector, and simply "pops_back" the last one each time a new RPC call is issued by iguana (currently hacked into listunspent).

This technically removes the need for locking utxos with iguana also, but I haven't tried any of that at this stage. Although this is not a huge job to properly implement. 

Using utxo cache may not be required for everyone, but the idea is to give all nodes the ability to respond extremely quickly, and THEN ALSO WAIT for them. Just because we can respond slower, it does not mean that we should, as it will eventually slow things enough that notrizations will become unreliable. 
