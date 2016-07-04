#pragma once

#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCellPicker.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkDoubleArray.h>
#include <vtkExtractSelection.h>
#include <vtkInEdgeIterator.h>
#include <vtkMath.h>
#include <vtkMutableUndirectedGraph.h>
#include <vtkPolyData.h>
#include <vtkPolyDataNormals.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkSelection.h>
#include <vtkSelectionNode.h>
#include <vtkSmartPointer.h>
#include <vtkSTLReader.h>
#include <vtkTriangle.h>
#include <vtkUnsignedCharArray.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <list>
#include <set>
#include <unordered_map>

#include "vtkConvertToDualGraph.h"

using namespace std;

typedef pair<vtkIdType, double> heapElem;

const double goldenRatio = 0.618033988749895;

class heapElemComp {
public:
    bool operator() (const heapElem& A, const heapElem& B) {
        return A.second < B.second || (A.second == B.second && A.first < B.first);
    }
};

class UserInteractionManager {
private:
    vtkSmartPointer<vtkPolyData> Data;
    vtkIdType numberOfFaces;

    double h, s, v;
    int clusterOkCnt, clusterCnt;
    unordered_map<int, bool> idHash;
    list<unsigned char*> clusterColors;
    list< vtkSmartPointer<vtkIdTypeArray> > clusterFaceIds;
    vtkSmartPointer<vtkUnsignedCharArray> faceColors;

public:
    UserInteractionManager() {}

    UserInteractionManager(vtkSmartPointer<vtkPolyData> Data) {
        this->Data = Data;

        numberOfFaces = Data->GetNumberOfCells();

        clusterOkCnt = 0;
        clusterCnt = 0;
        idHash = unordered_map<int, bool>();

        vtkMath::RandomSeed((unsigned)time(NULL));
        h = vtkMath::Random(0.0, 1.0);
        printf("h = %lf\n", h);

        s = 0.7;
        v = 0.8;
        unsigned char* clusterColor = HSVtoRGB(h, s, v);
        clusterColors.push_front(clusterColor);

        unsigned char white[3] = { 255, 255, 255 };
        faceColors = vtkSmartPointer<vtkUnsignedCharArray>::New();
        faceColors->SetNumberOfComponents(3);
        faceColors->SetNumberOfTuples(numberOfFaces);
        faceColors->SetName("Colors");
        for (vtkIdType i = 0; i < numberOfFaces; ++i) {
            faceColors->SetTupleValue(i, white);
        }
        Data->GetCellData()->SetScalars(faceColors);

        vtkSmartPointer<vtkIdTypeArray> clusterFaceId = vtkSmartPointer<vtkIdTypeArray>::New();
        clusterFaceId->SetNumberOfComponents(1);
        clusterFaceIds.push_front(clusterFaceId);
    }

    void StartSegmentation(const vtkSmartPointer<vtkRenderWindowInteractor>& interactor) {
        // preparation
        vtkSmartPointer<vtkPoints> points = Data->GetPoints();
        vtkSmartPointer<vtkDataArray> dataArray = points->GetData();

        vtkSmartPointer<vtkDoubleArray> centers, areas;
        vtkSmartPointer<vtkConvertToDualGraph> convert = vtkSmartPointer<vtkConvertToDualGraph>::New();
        convert->SetInputData(Data);
        convert->Update();

        vtkMutableUndirectedGraph *g = vtkMutableUndirectedGraph::SafeDownCast(convert->GetOutput());
        centers = vtkDoubleArray::SafeDownCast(g->GetVertexData()->GetArray("Centers"));
        areas = vtkDoubleArray::SafeDownCast(g->GetVertexData()->GetArray("Areas"));
        cout << "vertex number : " << g->GetNumberOfVertices() << endl;
        cout << "edge number : " << g->GetNumberOfEdges() << endl;

        // start clustering
        vtkSmartPointer<vtkDoubleArray> meshDis = vtkDoubleArray::SafeDownCast(g->GetEdgeData()->GetArray("Weights"));

        list<vtkIdType> lastCenterIds;
        unordered_map<int, double*> distances;
        int iterationCnt = 0;

        for (vtkIdType i = 0; i <= clusterCnt; ++i) {
            lastCenterIds.push_back(-1);
        }

        double **possibilities = new double*[numberOfFaces];
        while (iterationCnt < 10) {
            printf("Clutering --- iteration %d . . .\n", (iterationCnt + 1));

            list<vtkIdType> clusterCenterIds;

            // get center of each cluster
            list< vtkSmartPointer<vtkIdTypeArray> >::iterator faceIt = clusterFaceIds.begin();
            list<vtkIdType>::iterator lastIt = lastCenterIds.begin();
            bool continueFlag = false;

            for (vtkIdType i = 0; i <= clusterCnt; ++i) {
                vtkIdType tmpId;
                getCenterFaceId(*faceIt, centers, tmpId);

                if (tmpId != *lastIt) {
                    continueFlag = true;
                }
                *lastIt = tmpId;

                clusterCenterIds.push_back(tmpId);

                if (!distances[tmpId]) {
                    distances[tmpId] = getDijkstraTable(meshDis, tmpId, g);
                }

                *faceIt = vtkSmartPointer<vtkIdTypeArray>::New();

                ++faceIt;
                ++lastIt;
            }

            if (!continueFlag) {
                break;
            }

            list<vtkIdType> *minDisIds = new list<vtkIdType>[clusterCenterIds.size()];
            for (vtkIdType i = 0; i < numberOfFaces; ++i) {
                double minDis = DBL_MAX;
                vtkIdType minDisId;
                int clusterId = 0;
                for (list<vtkIdType>::iterator it = clusterCenterIds.begin(); it != clusterCenterIds.end(); ++it) {
                    if (distances[*it][i] < minDis) {
                        minDis = distances[*it][i];
                        minDisId = clusterId;
                    }
                    ++clusterId;
                }
                minDisIds[minDisId].push_back(i);
            }

            /* ========================================================= */
//             for (vtkIdType i = 0; i < numberOfFaces; ++i) {
//                 possibilities[i] = new double[clusterCnt + 1];
//                 double tmp = 0;
// 
//                 list<vtkIdType>::iterator centerIt = clusterCenterIds.begin();
//                 for (vtkIdType j = 0; j <= clusterCnt; ++j) {
//                     tmp += (1.0 / distances[*centerIt][i]);
//                     ++centerIt;
//                 }
// 
//                 centerIt = clusterCenterIds.begin();
//                 for (vtkIdType j = 0; j <= clusterCnt; ++j) {
//                     if (distances[*centerIt][i] == 0.0) {
//                         possibilities[i][j] = 1.0;
//                     } else {
//                         possibilities[i][j] = 1.0 / (distances[*centerIt][i] * tmp);
//                     }
//                     ++centerIt;
//                 }
// 
//                 double tmpColor[3] = { 0.0, 0.0, 0.0 };
//                 list<unsigned char*>::iterator colorIt = clusterColors.begin();
//                 for (vtkIdType j = 0; j <= clusterCnt; ++j) {
//                     tmpColor[0] += possibilities[i][j] * (*colorIt)[0];
//                     tmpColor[1] += possibilities[i][j] * (*colorIt)[1];
//                     tmpColor[2] += possibilities[i][j] * (*colorIt)[2];
//                     ++colorIt;
//                 }
// 
//                 unsigned char color[3] = { (unsigned char) tmpColor[0], (unsigned char) tmpColor[1], (unsigned char) tmpColor[2] };
//                 faceColors->SetTupleValue(i, color);
//             }
//             Data->GetCellData()->RemoveArray("Colors");
//             Data->GetCellData()->SetScalars(faceColors);
//             interactor->GetRenderWindow()->Render();
            /* ========================================================= */

            list< vtkSmartPointer<vtkIdTypeArray> >::iterator faceIdIt = clusterFaceIds.begin();
            for (vtkIdType i = 0; i <= clusterCnt; ++i) {
                for (list< vtkIdType >::iterator it = minDisIds[i].begin(); it != minDisIds[i].end(); ++it) {
                    (*faceIdIt)->InsertNextValue(*it);
                }
                ++faceIdIt;
            }

            // re-render clusters
            list<unsigned char*>::iterator colorIt;
            list< vtkSmartPointer<vtkIdTypeArray> >::iterator idsIt;
            colorIt = clusterColors.begin();
            idsIt = clusterFaceIds.begin();

            for (vtkIdType i = 0; i <= clusterCnt; ++i) {
                highlightFace(interactor, *idsIt, *colorIt);
                ++idsIt;
                ++colorIt;
            }

            ++iterationCnt;
        }

        printf("done!\n");
    }

    void SelectionDone() {
        ++clusterCnt;

        h += goldenRatio;
        if (h >= 1) {
            h -= 1;
        }
        
        unsigned char* clusterColor = HSVtoRGB(h, s, v);
        clusterColors.push_front(clusterColor);

        vtkSmartPointer<vtkIdTypeArray> clusterFaceId = vtkSmartPointer<vtkIdTypeArray>::New();
        clusterFaceId->SetNumberOfComponents(1);
        clusterFaceIds.push_front(clusterFaceId);
    }

    void Selecting(const vtkSmartPointer<vtkCellPicker>& picker, const vtkSmartPointer<vtkRenderWindowInteractor>& interactor) {
        vtkSmartPointer<vtkIdTypeArray>& ids = *(clusterFaceIds.begin());
        unsigned char* selectedColor = *(clusterColors.begin());

        if (picker->GetCellId() != -1) {
            if (!idHash[picker->GetCellId()]) {
                ids->InsertNextValue(picker->GetCellId());
                idHash[picker->GetCellId()] = true;
            }

            highlightFace(interactor, ids, selectedColor);
        }
    }

    unsigned char* HSVtoRGB(double h, double s, double v) {
        h *= 360.0;

        int tmp = floor(h / 60);
        double f = h / 60 - tmp;
        double p = v * (1 - s);
        double q = v * (1 - f * s);
        double t = v * (1 - (1 - f) * s);

        double* tmpArray = new double[3];
        unsigned char* res = new unsigned char[3];

        if (tmp == 0) {
            tmpArray[0] = v;
            tmpArray[1] = t;
            tmpArray[2] = p;
        } else if (tmp == 1) {
            tmpArray[0] = q;
            tmpArray[1] = v;
            tmpArray[2] = p;
        } else if (tmp == 2) {
            tmpArray[0] = p;
            tmpArray[1] = v;
            tmpArray[2] = t;
        } else if (tmp == 3) {
            tmpArray[0] = p;
            tmpArray[1] = q;
            tmpArray[2] = v;
        } else if (tmp == 4) {
            tmpArray[0] = t;
            tmpArray[1] = p;
            tmpArray[2] = v;
        } else {
            tmpArray[0] = v;
            tmpArray[1] = p;
            tmpArray[2] = q;
        }

        res[0] = (unsigned char)(tmpArray[0] * 256);
        res[1] = (unsigned char)(tmpArray[1] * 256);
        res[2] = (unsigned char)(tmpArray[2] * 256);

        return res;
    }

    double* getDijkstraTable(const vtkSmartPointer<vtkDoubleArray>& meshDis, int faceId, const vtkSmartPointer<vtkMutableUndirectedGraph>& g) {
        double *distances = new double[numberOfFaces];
        set<heapElem, heapElemComp> minHeap;

        // initialize distance
        for (vtkIdType j = 0; j < numberOfFaces; ++j) {
            distances[j] = DBL_MAX;
        }
        vtkSmartPointer<vtkInEdgeIterator> it = vtkSmartPointer<vtkInEdgeIterator>::New();
        g->GetInEdges(faceId, it);
        while (it->HasNext()) {
            vtkInEdgeType edge = it->Next();
            distances[edge.Source] = meshDis->GetValue(edge.Id);
        }
        distances[faceId] = 0.0;

        unordered_map<int, bool> S;
        S[faceId] = true;

        for (int j = 0; j < numberOfFaces; ++j) {
            if (faceId == j) {
                continue;
            }
            pair<vtkIdType, double> tmpPair(j, distances[j]);
            minHeap.insert(tmpPair);
        }

        while (minHeap.size()) {
            // u = EXTRACT_MIN(Q)
            vtkIdType u = minHeap.begin()->first;
            minHeap.erase(minHeap.begin());

            // S <- S union {u}
            S[u] = true;

            // for each vertex v in u's neighbor, do "relax" operation
            vtkSmartPointer<vtkInEdgeIterator> uIt = vtkSmartPointer<vtkInEdgeIterator>::New();
            g->GetInEdges(u, uIt);
            while (uIt->HasNext()) {
                vtkInEdgeType uEdge = uIt->Next();
                vtkIdType v = uEdge.Source;
                if (S[v]) {
                    continue;
                }

                double tmp = distances[u] + meshDis->GetValue(uEdge.Id);
                if (distances[v] > tmp) {
                    pair<vtkIdType, double> tmpPair(v, distances[v]);
                    minHeap.erase(minHeap.find(tmpPair));
                    distances[v] = tmp;
                    tmpPair = pair<vtkIdType, double>(v, distances[v]);
                    minHeap.insert(tmpPair);
                }
            }
        }

        return distances;
    }

    void highlightFace(const vtkSmartPointer<vtkRenderWindowInteractor>& interactor, const vtkSmartPointer<vtkIdTypeArray>& ids, unsigned char* color) {
        for (vtkIdType i = 0; i < ids->GetNumberOfTuples(); ++i) {
            faceColors->SetTupleValue(ids->GetValue(i), color);
        }
        Data->GetCellData()->RemoveArray("Colors");
        Data->GetCellData()->SetScalars(faceColors);
        interactor->GetRenderWindow()->Render();
    }

    void getCenterFaceId(const vtkSmartPointer<vtkIdTypeArray>& ids, const vtkSmartPointer<vtkDoubleArray>& centers, vtkIdType& centerId) {
        double center[3] = { 0.0, 0.0, 0.0 };
        for (vtkDataArrayTemplate<vtkIdType>::Iterator it = ids->Begin(); it < ids->End(); ++it) {
            center[0] += centers->GetTuple(*it)[0];
            center[1] += centers->GetTuple(*it)[1];
            center[2] += centers->GetTuple(*it)[2];
        }
        //cout << "size : " << ids->GetNumberOfTuples() << ", ";
        center[0] /= ids->GetNumberOfTuples();
        center[1] /= ids->GetNumberOfTuples();
        center[2] /= ids->GetNumberOfTuples();
        //printf("center : (%lf, %lf, %lf)\n", center[0], center[1], center[2]);

        double minDis = DBL_MAX;
        for (vtkIdType i = 0; i < centers->GetNumberOfTuples(); ++i) {
            double tmp[3] = { centers->GetTuple(i)[0], centers->GetTuple(i)[1], centers->GetTuple(i)[2] };
            double dis = vtkMath::Distance2BetweenPoints(center, tmp);
            if (dis < minDis) {
                minDis = dis;
                centerId = i;
            }
        }

        //printf("nearest --- id : %d, center : (%lf, %lf, %lf)\n", centerId, centers->GetTuple(centerId)[0], centers->GetTuple(centerId)[1],centers->GetTuple(centerId)[2]);
    }
};