/******************************************************/
/*                                                    */
/* edgeop.cpp - edge operation                        */
/*                                                    */
/******************************************************/
/* Copyright 2019 Pierre Abbat.
 * This file is part of PerfectTIN.
 *
 * PerfectTIN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * PerfectTIN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License and Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * and Lesser General Public License along with PerfectTIN. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <cstring>
#include <cassert>
#include "tin.h"
#include "angle.h"
#include "edgeop.h"
#include "octagon.h"
#include "triop.h"
#include "neighbor.h"
#include "threads.h"
#include "adjelev.h"

using namespace std;

map<int,pointlist> tempPointlist;
/* Small pointlists, 5 points and 4 triangles, used for deciding whether to
 * flip an edge. One per worker thread.
 */
double critLog[16];

void logCrit(double crit)
{
  memmove(critLog,critLog+1,15*sizeof(double));
  critLog[15]=crit;
}

void initTempPointlist(int nthreads)
{
  int i;
  for (i=0;i<nthreads;i++)
    tempPointlist[i];
}

void flip(edge *e)
{
  vector<xyz> allDots;
  int i;
  net.wingEdge.lock();
  e->flip(&net);
  //assert(net.checkTinConsistency());
  net.wingEdge.unlock();
  allDots.resize(e->tria->dots.size()+e->trib->dots.size());
  if (e->tria->dots.size())
    memmove(&allDots[0],&e->tria->dots[0],e->tria->dots.size()*sizeof(xyz));
  if (e->trib->dots.size())
    memmove(&allDots[e->tria->dots.size()],&e->trib->dots[0],e->trib->dots.size()*sizeof(xyz));
  e->tria->dots.clear();
  e->trib->dots.clear();
  for (i=0;i<allDots.size();i++)
    if (e->tria->in(allDots[i]))
      e->tria->dots.push_back(allDots[i]);
    else
      e->trib->dots.push_back(allDots[i]);
  e->tria->dots.shrink_to_fit();
  e->trib->dots.shrink_to_fit();
  e->tria->flatten();
  e->trib->flatten();
  e->tria->unsetError();
  e->trib->unsetError();
}

point *bend(edge *e)
/* Inserts a new point, bending and breaking the edge e of the perimeter,
 * then flips the edge e.
 */
{
  edge *anext=e,*bnext=e;
  int abear,ebear,bbear;
  net.wingEdge.lock();
  do
    anext=anext->next(e->a);
  while (anext->isinterior());
  do
    bnext=bnext->next(e->b);
  while (bnext->isinterior());
  abear=anext->bearing(e->a);
  ebear=e->bearing(e->a);
  bbear=bnext->bearing(e->b);
  while (abs(abear-ebear)>DEG90)
    abear+=DEG180;
  while (abs(bbear-ebear)>DEG90)
    bbear+=DEG180;
  abear=ebear+(abear-ebear)/2;
  bbear=ebear+(bbear-ebear)/2;
  point newPoint(intersection(*e->a,abear,*e->b,bbear),0);
  int newPointNum=net.points.size()+1;
  net.addpoint(newPointNum,newPoint);
  point *pnt=&net.points[newPointNum];
  int newEdgeNum=net.edges.size();
  net.edges[newEdgeNum  ].a=e->a;
  net.edges[newEdgeNum  ].b=pnt;
  net.edges[newEdgeNum+1].a=e->b;
  net.edges[newEdgeNum+1].b=pnt;
  pnt->line=&net.edges[newEdgeNum];
  net.edges[newEdgeNum  ].setnext(pnt,&net.edges[newEdgeNum+1]);
  net.edges[newEdgeNum+1].setnext(pnt,&net.edges[newEdgeNum  ]);
  int newTriNum=net.addtriangle();
  net.triangles[newTriNum].a=pnt;
  // If abear-bbear<0, then e is counterclockwise around the TIN.
  if (abear-bbear<0)
  {
    net.triangles[newTriNum].b=e->b;
    net.triangles[newTriNum].c=e->a;
    anext->setnext(e->a,&net.edges[newEdgeNum]);
    net.edges[newEdgeNum  ].setnext(e->a,e);
    e->setnext(e->b,&net.edges[newEdgeNum+1]);
    net.edges[newEdgeNum+1].setnext(e->b,bnext);
    e->tria=&net.triangles[newTriNum];
    net.edges[newEdgeNum  ].trib=&net.triangles[newTriNum];
    net.edges[newEdgeNum+1].tria=&net.triangles[newTriNum];
    net.insertHullPoint(pnt,e->a);
  }
  else
  {
    net.triangles[newTriNum].b=e->a;
    net.triangles[newTriNum].c=e->b;
    bnext->setnext(e->b,&net.edges[newEdgeNum+1]);
    net.edges[newEdgeNum+1].setnext(e->b,e);
    e->setnext(e->a,&net.edges[newEdgeNum]);
    net.edges[newEdgeNum  ].setnext(e->a,anext);
    e->trib=&net.triangles[newTriNum];
    net.edges[newEdgeNum  ].tria=&net.triangles[newTriNum];
    net.edges[newEdgeNum+1].trib=&net.triangles[newTriNum];
    net.insertHullPoint(pnt,e->b);
  }
  e->setNeighbors();
  //assert(net.checkTinConsistency());
  net.wingEdge.unlock();
  flip(e);
  return pnt;
}

bool spikyTriangle(xy a,xy b,xy c)
/* Returns true if the altitude of b is such that the angle A or C must be
 * less than a second of arc. Such a triangle should never appear in the TIN.
 */
{
  return dist(a,c)>412529.6*fabs(pldist(b,c,a));
}

bool shouldFlip(edge *e,double tolerance,int thread)
/* Decides whether to flip an interior edge. If it is flippable (that is,
 * the two triangles form a convex quadrilateral), it decides based on
 * a combination of four criteria:
 * 1. They would fit the dots better after flipping than before.
 * 2. The number of dots on each side would be better balanced after flipping.
 * 3. The area on each side would be better balanced after flipping.
 * 4. The edge would be shorter after flipping.
 */
{
  int i,j;
  array<triangle *,2> triab;
  triangle *tri;
  bool validTemp,ret=false,inTol,isSpiky,wouldbeSpiky;
  double elev13,elev24,elev5;
  double crit1=-999,crit2=0,crit3=0,crit4=0;
  double areas[4];
  int ndots[4];
  vector<triangle *> alltris;
  vector<point *> allpoints;
  tempPointlist[thread].clear();
  tempPointlist[thread].addpoint(1,*e->a);
  tempPointlist[thread].addpoint(2,*e->nexta->otherend(e->a));
  tempPointlist[thread].addpoint(3,*e->b);
  tempPointlist[thread].addpoint(4,*e->nextb->otherend(e->b));
  tempPointlist[thread].addpoint(5,point(intersection(*e->a,*e->b,
			  *e->nextb->otherend(e->b),*e->nexta->otherend(e->a)),0));
  isSpiky=spikyTriangle(tempPointlist[thread].points[1],
			tempPointlist[thread].points[2],
			tempPointlist[thread].points[3]) ||
	  spikyTriangle(tempPointlist[thread].points[3],
			tempPointlist[thread].points[4],
			tempPointlist[thread].points[1]);
  wouldbeSpiky=spikyTriangle(tempPointlist[thread].points[4],
			     tempPointlist[thread].points[1],
			     tempPointlist[thread].points[2]) ||
	       spikyTriangle(tempPointlist[thread].points[2],
			     tempPointlist[thread].points[3],
			     tempPointlist[thread].points[4]);
  if (isSpiky && wouldbeSpiky)
    cout<<"spiky triangle\n";
  inTol=e->tria->inTolerance(tolerance)&&e->trib->inTolerance(tolerance);
  if (!inTol)
  {
    for (i=0;i<4;i++)
    {
      tempPointlist[thread].edges[i].a=&tempPointlist[thread].points[i+1];
      tempPointlist[thread].edges[i].b=&tempPointlist[thread].points[(i+1)%4+1];
      tempPointlist[thread].edges[i+4].a=&tempPointlist[thread].points[i+1];
      tempPointlist[thread].edges[i+4].b=&tempPointlist[thread].points[5];
    }
    for (i=0;i<4;i++)
    {
      tempPointlist[thread].edges[i].nexta=&tempPointlist[thread].edges[(i+3)%4];
      tempPointlist[thread].edges[i].nextb=&tempPointlist[thread].edges[(i+1)%4+4];
      tempPointlist[thread].edges[i+4].nexta=&tempPointlist[thread].edges[i];
      tempPointlist[thread].edges[i+4].nextb=&tempPointlist[thread].edges[(i+3)%4+4];
    }
    for (i=1;i<6;i++)
      tempPointlist[thread].points[i].line=&tempPointlist[thread].edges[(i-1)%4+4];
    tempPointlist[thread].maketriangles();
    validTemp=tempPointlist[thread].checkTinConsistency();
    /*
    *           2
    *         / | \
    *      0/   5   \1
    *     /  0  |  1  \
    *   /       |       \
    * 1----4----5----6----3
    *   \       |       /
    *     \  3  |  2  /
    *      3\   7   /2
    *         \ | /
    *           4
    * Line 1-3 is the edge before flipping; line 2-4 is what it would be after.
    * It is possible for valid splittings to produce an invalid temporary pointlist.
    * Split △ABC at D, then split △ABD at E. C, D, and E are collinear. Then try
    * to flip BD. The temporary pointlist looks like this:
    * 2
    * | \
    * |   \
    * |  1  \
    * |       \
    * 1=5-------3
    * |       /
    * |  2  /
    * |   /
    * | /
    * 4
    * Because of roundoff error, it may appear to be flippable, but in fact is not.
    */
    for (i=0;i<4;i++)
      areas[i]=area3(tempPointlist[thread].points[(i+1)%4+1],
		    tempPointlist[thread].points[(i)%4+1],
		    tempPointlist[thread].points[5]);
    crit3=(fabs(areas[0]+areas[1]-areas[2]-areas[3])-fabs(areas[0]-areas[1]-areas[2]+areas[3]))/
	  (areas[0]+areas[1]+areas[2]+areas[3]);
    triab[0]=e->tria;
    triab[1]=e->trib;
    if (validTemp)
    {
      tri=&tempPointlist[thread].triangles[0];
      for (i=0;i<2;i++)
	for (j=0;j<triab[i]->dots.size();j++)
	{
	  tri=tri->findt(triab[i]->dots[j],true);
	  tri->dots.push_back(triab[i]->dots[j]);
	}
      for (i=1;i<6;i++)
	allpoints.push_back(&tempPointlist[thread].points[i]);
      for (i=0;i<4;i++)
	alltris.push_back(&tempPointlist[thread].triangles[i]);
      if (adjustElev(alltris,allpoints).validMatrix)
      {
	elev13=(tempPointlist[thread].points[1].elev()*tempPointlist[thread].edges[6].length()+
		tempPointlist[thread].points[3].elev()*tempPointlist[thread].edges[4].length())/
	      (tempPointlist[thread].edges[4].length()+tempPointlist[thread].edges[6].length());
	elev24=(tempPointlist[thread].points[2].elev()*tempPointlist[thread].edges[7].length()+
		tempPointlist[thread].points[4].elev()*tempPointlist[thread].edges[5].length())/
	      (tempPointlist[thread].edges[5].length()+tempPointlist[thread].edges[7].length());
	elev5=tempPointlist[thread].points[5].elev();
      }
      else
	elev13=elev24=elev5=0;
      if (elev13==elev24)
	crit1=0;
      else
	crit1=(fabs(elev13-elev5)-fabs(elev24-elev5))/(fabs(elev13-elev5)+fabs(elev24-elev5));
      for (i=0;i<4;i++)
	ndots[i]=tempPointlist[thread].edges[i].tria->dots.size();
      crit2=(abs(ndots[0]+ndots[1]-ndots[2]-ndots[3])-abs(ndots[0]-ndots[1]-ndots[2]+ndots[3]))/
	    (ndots[0]+ndots[1]+ndots[2]+ndots[3]+1.);
      crit4=(tempPointlist[thread].edges[4].length()-
	    tempPointlist[thread].edges[5].length()+
	    tempPointlist[thread].edges[6].length()-
	    tempPointlist[thread].edges[7].length())/
	    (tempPointlist[thread].edges[4].length()+
	    tempPointlist[thread].edges[5].length()+
	    tempPointlist[thread].edges[6].length()+
	    tempPointlist[thread].edges[7].length());
      ret=crit1+crit2+crit3+crit4>0;
    }
    logCrit(crit1);
    logCrit(crit2);
    logCrit(crit3);
    logCrit(crit4);
  }
  if (isSpiky && !wouldbeSpiky)
    ret=true;
  if (wouldbeSpiky && !isSpiky)
    ret=false;
  return ret;
}

bool shouldBend(edge *e,double tolerance)
{
  if (e->tria)
    return shouldSplit(e->tria,tolerance);
  else
    return shouldSplit(e->trib,tolerance);
}

int edgeop(edge *e,double tolerance,int thread)
{
  bool did=false;
  bool gotLock1,gotLock2=true;
  vector<point *> corners;
  vector<triangle *> triNeigh,triAdj;
  corners.push_back(e->a);
  corners.push_back(e->b);
  if (e->tria)
  {
    triAdj.push_back(e->tria);
    corners.push_back(e->nextb->otherend(e->b));
  }
  if (e->trib)
  {
    triAdj.push_back(e->trib);
    corners.push_back(e->nexta->otherend(e->a));
  }
  gotLock1=lockTriangles(thread,triAdj);
  if (gotLock1 && e->isinterior())
    if (e->isFlippable() && shouldFlip(e,tolerance,thread))
    {
      triNeigh=triangleNeighbors(corners);
      gotLock2=lockTriangles(thread,triNeigh);
      if (gotLock2)
      {
	flip(e);
	did=true;
      }
    }
    else;
  else
    if (gotLock1 && shouldBend(e,tolerance))
    {
      triNeigh=triangleNeighbors(corners);
      gotLock2=lockTriangles(thread,triNeigh);
      if (gotLock2)
      {
	corners.push_back(bend(e));
	triNeigh=triangleNeighbors(corners);
	did=true;
      }
    }
  if (triNeigh.size()==0)
  {
    triNeigh=triangleNeighbors(corners);
    gotLock2=lockTriangles(thread,triNeigh);
  }
  if (gotLock2 && (did || std::isnan(rmsAdjustment())))
    logAdjustment(adjustElev(triNeigh,corners));
  unlockTriangles(thread);
  return gotLock1*2+gotLock2; // 2 means deadlock
}
