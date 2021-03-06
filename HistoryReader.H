//////////////////////////////////////////////////////////////////////
// Copyright 2014-2016 Jeffrey Comer
//
// This file is part of DiffusionFusion.
//
// DiffusionFusion is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
//
// DiffusionFusion is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with DiffusionFusion. If not, see http://www.gnu.org/licenses/.
///////////////////////////////////////////////////////////////////////
// Author: Jeff Comer <jeffcomer at gmail>
#ifndef HISTORYREADER_H
#define HISTORYREADER_H

#include "PiecewiseCubic.H"

class HistoryReader {
private:
  PiecewiseCubic** fieldList;
  int fieldNum;

public:
  HistoryReader(const char* histFile) {
    // Get the number of fields that we need.
    fieldNum = countSegments(histFile);
    fieldList = new PiecewiseCubic*[fieldNum];

    fieldNum = readHistFile(histFile, fieldList);
  }

  ~HistoryReader() {
    for (int f = 0; f < fieldNum; f++) delete fieldList[f];
    delete[] fieldList;
  }

  int length() const {
    return fieldNum;
  }

  const PiecewiseCubic* getBiasFrame(int frame) const {
    if (frame < 0) frame = 0;
    else if (frame >= fieldNum) frame = fieldNum-1;

    return fieldList[frame];
  }

  void smooth(double width) {
    for (int f = 0; f < fieldNum; f++) {
      double dr = fieldList[f]->getDr();
      double decay = (dr*dr)/(2.0*width*width);
      // Using a sampling window of 8 standard deviations.
      int winHalf = int(8.0*width/dr)/2;

      const int n = fieldList[f]->length();
      // Get the initial values.
      double data[n];
      for (int i = 0; i < n; i++) data[i] = fieldList[f]->get(i);

      // Gaussian filter.
      for (int i = 0; i < n; i++) {
	int j0 = i - winHalf;
	int j1 = i + winHalf;
	if (j0 < 0) j0 = 0;
	if (j1 >= n) j1 = n-1;

	double sum = 0.0;
	double sumW = 0.0;
	for (int j = j0; j <= j1; j++) {
	  double wj = exp(-(j-i)*(j-i)*decay);
	  sumW += wj;
	  sum += wj*data[j];
	}

	fieldList[f]->set(i,sum/sumW);
      }
    }
  }

  int enforceFullSamples(const char* countHistFile, int fullSamples) {
    PiecewiseCubic** ppCount = new PiecewiseCubic*[fieldNum];
    int changed = 0;

    readHistFile(countHistFile, ppCount);
    for (int f = 0; f < fieldNum; f++) {
      const int n = ppCount[f]->length();
      if (n != fieldList[f]->length()) {
	fprintf(stderr,"ERROR: HistoryReader::enforceFullSamples count history file `%s' frame %d does not have the same number of entries as the associated bias file (%d vs %d).\n", countHistFile, f, n, fieldList[f]->length());
	exit(-1);
      }
      
      // Zero the bias if the count is less than fullSamples/2.
      // Below full samples, the force turns on smoothly.
      // From colvarbias_abf.C of the NAMD source code:
      // Factor that ensures smooth introduction of the force
      /// if ( count < full_samples ) {
      /// fact = ( count < min_samples) ? 0.0 :
      /// (cvm::real (count - min_samples)) / (cvm::real (full_samples - min_samples)); }
      // J. Henin and G. Fiorin enforce a zero mean for periodic variables.
      // I'll skip that for now.
      int minSamples = fullSamples/2;
      for (int i = 0; i < n; i++) {
	int count = int(ppCount[f]->get(i));
	if (count < fullSamples) {
	  // We scale the bias with respect to fullSamples as in the colvars module.
	  double fact = ( count < minSamples )? 0.0 : ((double(count - minSamples)) / double(fullSamples - minSamples));

	  // Scale the bias by fact.
	  fieldList[f]->set(i,fact*fieldList[f]->get(i));
	  changed++;
	}
      } // end field node loop.
    } // end field loop

    for (int f = 0; f < fieldNum; f++) delete ppCount[f];
    delete[] ppCount;
    return changed;
  }

  int readHistFile(const char* histFile, PiecewiseCubic** ppf) {
    FILE* inp = fopen(histFile, "r");
    char line[STRLEN];
    
    bool inComment = false;
    String seg;
    
    int f = 0;
    while (fgets(line, STRLEN, inp) != NULL) {
      int len = strlen(line);
      // Ignore blank lines.
      if (len < 2) continue;

      if (line[0] == '#') {
	if (!inComment && seg.length() > 2) {
	  // Sanity check.
	  if (f >= fieldNum) {
	    fprintf(stderr,"ERROR HistoryReader::HistoryReader More segments found in the file `%s' than suggested by countSegments() (%d)\n", histFile, fieldNum);
	    exit(-1);
	  }

	  // We've reached the end of a data block.
	  // Try to load the field.
	  // We'll just make the bias not periodic since we only ever use
	  // get() and computeZeroOrder() anyway.
	  ppf[f] = new PiecewiseCubic(false, seg);
	  seg.clear();
	  f++;
	}
	inComment = true;
      } else {
	// If this isn't a comment, we add the line to the string.
	seg.add(line);
	inComment = false;
      }
    }

    // We may have one field left over.
    if (!inComment && seg.length() > 2) {
      // Sanity check.
      if (f >= fieldNum) {
	fprintf(stderr,"ERROR HistoryReader::HistoryReader More segments found in the file `%s' than suggested by countSegments() (%d)\n", histFile, fieldNum);
	exit(-1);
      }

      // We've reached the end of a data block.
      // Try to load the field.
      ppf[f] = new PiecewiseCubic(false, seg);
      seg.clear();
      f++;
    }
    fclose(inp);

    return f;
  }

  // Find the number of comment (#) separated segments.
  static int countSegments(const char* fileName) {
    FILE* inp = fopen(fileName, "r");
    if (inp == NULL) {
	fprintf(stderr,"ERROR HistoryReader::countSegments Cannot open file `%s' for reading.\n", fileName);
	exit(-1);
    }

    char line[STRLEN];
    int count = 0;
    bool inComment = false;
    int segments = 0;

    while (fgets(line, STRLEN, inp) != NULL) {
      int len = strlen(line);
      // Ignore blank lines.
      if (len == 0) continue;
      
      if (line[0] == '#') inComment = true;
      else {
	if (inComment) {
	  segments++;
	  inComment = false;
	}
	count++;
      }
    }
    fclose(inp);

    return segments+1;
  }

};

#endif
