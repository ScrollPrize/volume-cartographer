#pragma once

#include "CVolumeViewerWithCurve.hpp"



class PathChangeCommand : public QUndoCommand
{
public:
    PathChangeCommand(CVolumeViewerWithCurve* viewer, SegmentationStruct* segStruct, const PathChangePointVector before, const PathChangePointVector after, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;

protected:
    CVolumeViewerWithCurve* viewer;
    SegmentationStruct* segStruct;
    PathChangePointVector before;
    PathChangePointVector after;
};

class EvenlySpaceCurveCommand : public PathChangeCommand
{
public:
    EvenlySpaceCurveCommand(CVolumeViewerWithCurve* viewer, SegmentationStruct* segStruct, const PathChangePointVector before, const PathChangePointVector after, QUndoCommand* parent = nullptr);

    void undo() override;
    void redo() override;
};

