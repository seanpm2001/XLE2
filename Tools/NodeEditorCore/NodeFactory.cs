﻿// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using System;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.ComponentModel.Composition.Hosting;
using System.Linq;
using System.Drawing;
using HyperGraph;

#pragma warning disable 0649        // Field '...' is never assigned to, and will always have its default value null

namespace NodeEditorCore
{
    public enum InterfaceDirection { In, Out };

    public enum ProcedureNodeType { Normal, TemplateParameter, Instantiation };
    public enum ParamSourceType { Material, System, Output, Constant };

    public interface IDiagramDocument
    {
        GUILayer.NodeGraphFile NodeGraphFile { get; }
        GUILayer.NodeGraphMetaData GraphMetaData { get; }
        uint GlobalRevisionIndex { get; }
    }

    public interface IEditingContext
    {
        IDiagramDocument Document { get; }
    }

    public interface IPreviewMaterialContext
    {
        string ActivePreviewMaterialNames { get; }
    }

    public interface INodeAmender
    {
        void AmendNode(GUILayer.NodeGraphFile diagramContext, Node node, ProcedureNodeType type, IEnumerable<object> dataPackets);
    }

    internal static class Utils
    {
        internal static T AsEnumValue<T>(String input, IDictionary<Enum, string> table) where T : struct, IComparable, IConvertible, IFormattable
        {
            Type enumType = typeof(T);
            if (!enumType.IsEnum)
                throw new InvalidOperationException("Expecting enum type as parameter to AsEnumValue.");

            foreach (KeyValuePair<Enum, string> kvp in table)
                if (kvp.Value == input)
                    return (T)(object)kvp.Key;
            return default(T);
        }

        internal static Tuple<List<String>, int> AsEnumList(Enum value, IDictionary<Enum, string> table)
        {
            int selectedIndex = 0;
            List<String> typeNames = new List<String>();
            foreach (KeyValuePair<Enum, string> kvp in table)
            {
                if (kvp.Key.Equals(value))
                    selectedIndex = typeNames.Count;
                typeNames.Add(kvp.Value);
            }
            return Tuple.Create(typeNames, selectedIndex);
        }

        internal static String AsString(Enum e, IDictionary<Enum, string> table)
        {
            foreach (KeyValuePair<Enum, string> kvp in table)
                if (kvp.Key == e)
                    return kvp.Value;
            throw new InvalidOperationException(String.Format("Could not convert enum to string value {0}.", e));
        }
    }

    #region Node Items

    //
    /////////////////////////////////////////////////////////////////////////////////////
    //
    //          ShaderFragmentNodeConnector
    //
    //      Node Item for the hypergraph that represents an input or output
    //      of a shader graph node.
    //  

    public class ShaderFragmentNodeConnector : NodeConnector
    {
        public ShaderFragmentNodeConnector(string name, string type)
        {
            this.Name = name;
            this.Type = type;
        }

        #region Properties
        string internalText = string.Empty;
        public string Name
        {
            get { return internalText; }
            set
            {
                if (internalText == value)
                    return;
                internalText = value;
                TextSize = Size.Empty;
            }
        }
        internal string _shortType;
        internal string _typeText;
        public string Type
        {
            get { return _typeText; }
            set
            {
                _typeText = value;
                if (value != null && value.Length > 0)
                {
                    _shortType = value[0].ToString();
                    if (value.Count() > 0 && Char.IsDigit(value[value.Count() - 1]))
                    {
                        if (value.Count() > 2 && value[value.Count() - 2] == 'x' && Char.IsDigit(value[value.Count() - 3]))
                        {
                            _shortType += value.Substring(value.Count() - 3);
                        }
                        else
                        {
                            _shortType += value.Substring(value.Count() - 1);
                        }
                    }
                }
                else
                {
                    _shortType = "";
                }
                Tag = value;
            }
        }
        public string ShortType { get { return _shortType; } }
        internal SizeF TextSize;
        #endregion

        public override SizeF Measure(Graphics graphics, object context)
        {
                // Resize based on the length of the strings. Sometimes we get really
                // long names, so it's useful to resize the connector to match...!
            var mainStringSize = graphics.MeasureString(GetNameText(), SystemFonts.MenuFont);
            var shortTypeSize = graphics.MeasureString(GetTypeText(), SystemFonts.MenuFont);
            const uint separation = 8;
            return new SizeF(
                mainStringSize.Width + separation + shortTypeSize.Width, 
                Math.Max(GraphConstants.MinimumItemHeight, mainStringSize.Height));
        }
        
        public override void Render(Graphics graphics, RectangleF bounds, object context)
        {
            graphics.DrawString(
                this.Name, SystemFonts.MenuFont, Brushes.White,
                bounds, GraphConstants.LeftTextStringFormat);

            var mainStringSize = graphics.MeasureString(GetNameText(), SystemFonts.MenuFont);
            const uint separation = 8;

            RectangleF newRect = new RectangleF(bounds.Location, bounds.Size);
            newRect.X += mainStringSize.Width + separation;
            newRect.Width -= mainStringSize.Width + separation;
            graphics.DrawString(GetTypeText(), SystemFonts.MenuFont, Brushes.DarkGray,
                newRect, GraphConstants.LeftTextStringFormat);
        }

        internal virtual string GetNameText() { return this.Name; }
        internal virtual string GetTypeText() { return "(" + this.ShortType + ")"; }

        internal static Node.Dock GetDirectionalColumn(InterfaceDirection direction)
        {
            switch (direction)
            {
                case InterfaceDirection.In: return Node.Dock.Input;
                default:
                case InterfaceDirection.Out: return Node.Dock.Output;
            }
        }

        internal static GUILayer.NodeGraphSignature.ParameterDirection AsParameterDirection(InterfaceDirection direction)
        {
            switch (direction)
            {
                case InterfaceDirection.In: return GUILayer.NodeGraphSignature.ParameterDirection.In;
                default:
                case InterfaceDirection.Out: return GUILayer.NodeGraphSignature.ParameterDirection.Out;
            }
        }
    }

        //
    /////////////////////////////////////////////////////////////////////////////////////
        //
        //          ShaderFragmentPreviewItem
        //
        //      Node item for shader fragment preview... Renders and image via D3D
        //      and displays it in a little preview
        //  

    public class ShaderFragmentPreviewItem : NodeItem 
    {
        public ShaderFragmentPreviewItem()
        {
            _previewImage = null;
            _compiledPatchCollection = null;
            Geometry = GUILayer.PreviewGeometry.Plane2D;
            OutputToVisualize = "";
            _shaderStructureHash = 0;
        }

        private int pendingCounter = 0;

        public override void Render(Graphics graphics, RectangleF boundary, object context)
        {
            if (Node.Tag is ShaderFragmentNodeTag)
            {
                if (!graphics.IsVisible(boundary))
                    return;

                // We need to get some global context information 
                // (such the the containing graph and material parameter settings)
                // We could use composition to get access to a global. But ideally this
                // should be passed in from the top-level render function.
                // This is a more convenient way to track invalidation, also -- because we
                // can just check if there have been any changes since our last cached bitmap.

                var editingContext = context as IEditingContext;
                if (editingContext == null) return;

                uint currentHash = editingContext.Document.GlobalRevisionIndex;
                if (currentHash != _shaderStructureHash)
                {
                    _previewImage = null;
                    _compiledPatchCollection = null;
                }

                if (_compiledPatchCollection == null)
                {
                    var config = new GUILayer.NodeGraphPreviewConfiguration
                    {
                        _nodeGraph = editingContext.Document.NodeGraphFile,
                        _subGraphName = Node.SubGraphTag as string,
                        _previewNodeId = ((ShaderFragmentNodeTag)Node.Tag).Id
                    };
                    var actualizationMsgs = new GUILayer.MessageRelayWrapper();
                    _compiledPatchCollection = GUILayer.ShaderGeneratorLayer.MakeCompiledShaderPatchCollection(editingContext.Document.GraphMetaData, config, actualizationMsgs);
                }

                // (assuming no rotation on this transformation -- scale is easy to find)
                Size idealSize = new Size((int)(graphics.Transform.Elements[0] * boundary.Size.Width), (int)(graphics.Transform.Elements[3] * boundary.Size.Height));
                if (_previewImage != null)
                {
                    // compare the current bitmap size to the size we'd like
                    Size bitmapSize = _previewImage.Size;
                    float difference = System.Math.Max(System.Math.Abs(1.0f - bitmapSize.Width / (float)(idealSize.Width)), System.Math.Abs(1.0f - bitmapSize.Height / (float)(idealSize.Height)));
                    if (difference > 0.1f)
                        _previewImage = null;
                }

                if (_previewImage == null)
                {
                    var actualizationMsgs = new GUILayer.MessageRelayWrapper();
                    var techniqueDelegate = GUILayer.ShaderGeneratorLayer.MakeShaderPatchAnalysisDelegate(PreviewSettings, editingContext.Document.GraphMetaData.Variables, actualizationMsgs);

                    var matVisSettings = new GUILayer.MaterialVisSettings();
                    switch (Geometry)
                    {
                    case GUILayer.PreviewGeometry.Chart: matVisSettings.Geometry = GUILayer.MaterialVisSettings.GeometryType.Chart; break;
                    case GUILayer.PreviewGeometry.Plane2D: matVisSettings.Geometry = GUILayer.MaterialVisSettings.GeometryType.Plane2D; break;
                    case GUILayer.PreviewGeometry.Box: matVisSettings.Geometry = GUILayer.MaterialVisSettings.GeometryType.Cube; break;
                    case GUILayer.PreviewGeometry.Model: matVisSettings.Geometry = GUILayer.MaterialVisSettings.GeometryType.Model; break;
                    default:
                    case GUILayer.PreviewGeometry.Sphere: matVisSettings.Geometry = GUILayer.MaterialVisSettings.GeometryType.Sphere; break;
                    }

                    _previewImage = _previewManager.BuildPreviewImage(
                        matVisSettings, _previewMaterialContext?.ActivePreviewMaterialNames,
                        techniqueDelegate, _compiledPatchCollection, idealSize);
                    _shaderStructureHash = currentHash;
                }

                if (_previewImage != null)
                {
                    if (_previewImage.IsPending)
                    {
                        DrawPendingImage(graphics, boundary, pendingCounter);
                        ++pendingCounter;
                    }
                    else
                    {
                        pendingCounter = 0;
                        var finalImage = _previewImage.Bitmap;
                        if (finalImage != null)
                        {
                            if (Geometry == GUILayer.PreviewGeometry.Sphere)
                            {
                                var clipPath = new System.Drawing.Drawing2D.GraphicsPath();
                                clipPath.AddEllipse(boundary);
                                graphics.SetClip(clipPath);
                                graphics.DrawImage(finalImage, boundary);
                                graphics.ResetClip();
                            }
                            else
                            {
                                graphics.DrawImage(finalImage, boundary);
                            }
                        }
                        else
                        {
                            graphics.DrawString(
                                string.IsNullOrEmpty(_previewImage.StatusMessage) ? "No message" : _previewImage.StatusMessage,
                                new Font("Arial", 9),
                                new SolidBrush(Color.White),
                                boundary);
                        }
                    }
                }
            }
        }

        private void DrawPendingImage(Graphics graphics, RectangleF boundary, int pendingCounter)
        {
            const int circleCount = 21;
            const float circleRadius = 10.0f;

            PointF center = new PointF(
                (boundary.Left + boundary.Right) / 2.0f,
                (boundary.Top + boundary.Bottom) / 2.0f);
            float bigRadius = Math.Min(boundary.Width, boundary.Height) / 2.0f - circleRadius;

            float phase = (float)(pendingCounter / 100.0 * 2.0f * Math.PI);
            for (int c=0; c<circleCount; ++c)
            {
                float x = (float)Math.Cos(c / (double)circleCount * 2.0 * Math.PI + phase) * bigRadius;
                float y = (float)Math.Sin(c / (double)circleCount * 2.0 * Math.PI + phase) * bigRadius;
                float flux = (float)Math.Cos((c / 7.0 + pendingCounter / 20.0) * 2.0 * Math.PI);
                float brightness = flux * 0.25f + 0.5f;
                float radiusFlux = flux * 0.15f + 0.85f;

                RectangleF circleRect = new RectangleF(
                    center.X + x - radiusFlux * circleRadius, center.Y + y - radiusFlux * circleRadius,
                    2.0f * radiusFlux * circleRadius, 2.0f * radiusFlux * circleRadius);
                
                Brush b = new SolidBrush(Color.FromArgb(0xff, (int)(brightness * 255.0f), (int)(brightness * 255.0f), (int)(brightness * 255.0f)));
                graphics.FillEllipse(b, circleRect);
            }
        }

        public override bool HasActiveAnimation { get { return (Node.Tag is ShaderFragmentNodeTag) && ((_previewImage == null) || _previewImage.IsPending); } }

        public override SizeF   Measure(Graphics graphics, object context) { return new SizeF(196, 196); }

        public void InvalidateShaderStructure() { _previewImage = null; _shaderStructureHash = 0;  }
        public void InvalidateParameters() { _previewImage = null; }

        public override bool OnStartDrag(PointF location, out PointF original_location)
        {
            base.OnStartDrag(location, out original_location);
            _lastDragLocation = original_location = location;
            return true;
        }

        public override bool OnDrag(PointF location)
        {
            base.OnDrag(location);
            // PreviewRender.Manager.Instance.RotateLightDirection(_document, new PointF(location.X - _lastDragLocation.X, location.Y - _lastDragLocation.Y));
            _lastDragLocation = location;
            // InvalidateParameters();
            return true;
        }

        public override bool OnEndDrag() { base.OnEndDrag(); return true; }

        // note -- 
        //  we really want to do an InvokeMiscChange on the IGraphModel when these change...
        //  but we don't have any way to find the graph model from here!
        public GUILayer.PreviewGeometry Geometry  { get { return _previewGeometry; }      set { if (_previewGeometry != value) { _previewGeometry = value; InvalidateShaderStructure(); } } }
        public string OutputToVisualize                     { get { return _outputToVisualize; }    set { if (_outputToVisualize != value) { _outputToVisualize = (value!=null)?value:string.Empty; InvalidateShaderStructure(); } } }

        public GUILayer.PreviewSettings PreviewSettings
        {
            get
            {
                return new GUILayer.PreviewSettings
                    {
                        Geometry = this.Geometry,
                        OutputToVisualize = this.OutputToVisualize
                    };
            }

            set
            {
                Geometry = value.Geometry;
                OutputToVisualize = value.OutputToVisualize;
            }
        }

        private PointF _lastDragLocation;
        private GUILayer.PreviewGeometry _previewGeometry;
        private string _outputToVisualize;
        public GUILayer.PreviewBuilder _previewManager;
        public IPreviewMaterialContext _previewMaterialContext;

        // bitmap cache --
        private uint _shaderStructureHash;
        private GUILayer.PreviewBuilder.PreviewImage _previewImage;
        private GUILayer.CompiledShaderPatchCollectionWrapper _compiledPatchCollection;
    }

    [Export(typeof(INodeAmender))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class PreviewsNodeAmender : INodeAmender, IDisposable
    {
        public void AmendNode(GUILayer.NodeGraphFile diagramContext, Node node, ProcedureNodeType type, IEnumerable<object> dataPackets)
        {
            if (type != ProcedureNodeType.Normal || dataPackets == null)
                return;

            foreach (var o in dataPackets)
            {
                GUILayer.PreviewSettings previewSettings = o as GUILayer.PreviewSettings;
                if (previewSettings == null) continue;

                var previewItem = new ShaderFragmentPreviewItem();
                if (previewSettings != null)
                {
                    previewItem.PreviewSettings = previewSettings;
                    if (previewSettings.Geometry == GUILayer.PreviewGeometry.Sphere)
                        node.Layout = Node.LayoutType.Circular;
                }

                // use composition to access some exported types --
                previewItem._previewManager = _previewManager;
                previewItem._previewMaterialContext = _previewMaterialContext;
                node.AddItem(previewItem, Node.Dock.Center);

                // Drop-down selection box for "preview mode"
                var enumList = Utils.AsEnumList(previewItem.Geometry, PreviewGeoNames);
                var previewModeSelection = new HyperGraph.Items.NodeDropDownItem(enumList.Item1.ToArray(), enumList.Item2);
                node.AddItem(previewModeSelection, Node.Dock.Bottom);
                previewModeSelection.SelectionChanged +=
                    (object sender, HyperGraph.Items.AcceptNodeSelectionChangedEventArgs args) =>
                    {
                        if (sender is HyperGraph.Items.NodeDropDownItem)
                        {
                            var item = (HyperGraph.Items.NodeDropDownItem)sender;
                            previewItem.Geometry = Utils.AsEnumValue<GUILayer.PreviewGeometry>(item.Items[args.Index], PreviewGeoNames);
                            node.Layout = previewItem.Geometry == GUILayer.PreviewGeometry.Sphere ? Node.LayoutType.Circular : Node.LayoutType.Rectangular;
                        }
                    };

                // Text item box for output visualization string
                // note --  should could potentially become a more complex editing control
                //          it might be useful to have some preset defaults and helpers rather than just
                //          requiring the user to construct the raw string
                var outputToVisualize = new HyperGraph.Items.NodeTextBoxItem(previewItem.OutputToVisualize);
                node.AddItem(outputToVisualize, Node.Dock.Bottom);
                outputToVisualize.TextChanged +=
                    (object sender, HyperGraph.Items.AcceptNodeTextChangedEventArgs args) => { previewItem.OutputToVisualize = args.Text; };
            }
        }

        internal static IDictionary<Enum, string> PreviewGeoNames;

        static PreviewsNodeAmender()
        {
            PreviewGeoNames = new Dictionary<Enum, string>
            {
                { GUILayer.PreviewGeometry.Chart, "Chart" },
                { GUILayer.PreviewGeometry.Plane2D, "2D" },
                { GUILayer.PreviewGeometry.Box, "Box" },
                { GUILayer.PreviewGeometry.Sphere, "Sphere" },
                { GUILayer.PreviewGeometry.Model, "Model" }
            };
        }

        public PreviewsNodeAmender()
        {
            _previewManager = new GUILayer.PreviewBuilder();
        }

        ~PreviewsNodeAmender()
        {
            Dispose(false);
        }

        public void Dispose()
        {
            Dispose(true);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (_previewManager != null)
            {
                _previewManager.Dispose();
                _previewManager = null;
            }
        }

        private GUILayer.PreviewBuilder _previewManager;

        [Import]
        private IPreviewMaterialContext _previewMaterialContext;
    }

        //
    /////////////////////////////////////////////////////////////////////////////////////
        //
        //          ShaderFragmentNodeCompatibility
        //
        //      Checks node inputs and outputs for type compatibility
        //  

    public class ShaderFragmentNodeCompatibility : HyperGraph.Compatibility.ICompatibilityStrategy
    {
        public HyperGraph.Compatibility.ConnectionType CanConnect(NodeConnector from, NodeConnector to)
        {
            if (null == from.Tag && null == to.Tag) return HyperGraph.Compatibility.ConnectionType.Compatible;
            if (null == from.Tag || null == to.Tag) return HyperGraph.Compatibility.ConnectionType.Incompatible;
            if (null == from.Node.SubGraphTag  || from.Node.SubGraphTag != to.Node.SubGraphTag) return HyperGraph.Compatibility.ConnectionType.Incompatible;    // can't connect different subgraph

            if (from.Tag is string && to.Tag is string)
            {
                string fromType = (string)from.Tag;
                string toType = (string)to.Tag;

                    // we use "auto" when we want one end to match whatever is on the other end.
                    // so, "auto" can match to anything -- except another auto. Auto to auto is always incompatible.
                bool fromAuto = fromType.Equals("auto", StringComparison.CurrentCultureIgnoreCase);
                bool toAuto = toType.Equals("auto", StringComparison.CurrentCultureIgnoreCase);
                if (fromAuto || toAuto)
                    return (fromAuto && toAuto) ? HyperGraph.Compatibility.ConnectionType.Incompatible : HyperGraph.Compatibility.ConnectionType.Compatible;

                    // "graph" with a restriction in angle brackets, or graph alone, should match. We don't
                    // do compatibility testing for the restriction parameter
                if (fromType.StartsWith("graph", StringComparison.CurrentCultureIgnoreCase) && toType.StartsWith("graph", StringComparison.CurrentCultureIgnoreCase))
                    return HyperGraph.Compatibility.ConnectionType.Compatible;

                if (fromType.Equals(toType, StringComparison.CurrentCultureIgnoreCase))
                {
                    return HyperGraph.Compatibility.ConnectionType.Compatible;
                }
                else
                {
                    // Some types have automatic conversion operations
                    if (GUILayer.TypeRules.HasAutomaticConversion(fromType, toType))
                    {
                        return HyperGraph.Compatibility.ConnectionType.Conversion;
                    }
                }
            }

            return HyperGraph.Compatibility.ConnectionType.Incompatible;
        }
    }

    public class ShaderFragmentAdaptableParameterConnector : ShaderFragmentNodeConnector
    {
        public ShaderFragmentAdaptableParameterConnector(string name, string type) : base(name, type) { }
        public string Semantic { get; set; }
        public string Default { get; set; }
        public InterfaceDirection Direction { get; set; }
        internal override string GetTypeText() { return ": " + Semantic + " (" + ShortType + ")"; }
    }

    public class ShaderFragmentInterfaceParameterItem : ShaderFragmentAdaptableParameterConnector
    {
        public ShaderFragmentInterfaceParameterItem(string name, string type) : base(name, type) { }
    }

    public class ShaderFragmentCaptureParameterItem : ShaderFragmentAdaptableParameterConnector
    {
        public ShaderFragmentCaptureParameterItem(string name, string type) : base(name, type) { }
    }

    public class ShaderFragmentAddParameterItem : NodeItem
    {
        public delegate ShaderFragmentAdaptableParameterConnector ConnectorCreatorDelegate(string name, string type);
        public ConnectorCreatorDelegate ConnectorCreator;

        public override bool OnClick(System.Windows.Forms.Control container, System.Windows.Forms.MouseEventArgs evnt, System.Drawing.Drawing2D.Matrix viewTransform)
        {
            using (var fm = new InterfaceParameterForm(false) { Name = "Color", Type = "auto", Semantic = "", Default = "" })
            {
                var result = fm.ShowDialog();
                if (result == System.Windows.Forms.DialogResult.OK)
                {
                    // Remove and re-add "this to ensure it gets placed at the end
                    var node = Node;
                    node.RemoveItem(this);
                    var connector = ConnectorCreator(fm.Name, fm.Type);
                    connector.Semantic = fm.Semantic;
                    connector.Default = fm.Default;
                    node.AddItem(
                        // new ShaderFragmentInterfaceParameterItem(fm.Name, fm.Type) { Semantic = fm.Semantic, Default = fm.Default, Direction = Direction }, 
                        connector,
                        ShaderFragmentNodeConnector.GetDirectionalColumn(connector.Direction));
                    node.AddItem(this, ShaderFragmentNodeConnector.GetDirectionalColumn(connector.Direction));
                }
            }
            return true;
        }

        public override SizeF Measure(Graphics graphics, object context)
        {
            return new Size(GraphConstants.MinimumItemWidth, GraphConstants.MinimumItemHeight);
        }

        public override void Render(Graphics graphics, RectangleF boundary, object context)
        {
            using (var path = GraphRenderer.CreateRoundedRectangle(boundary.Size, boundary.Location))
            {
                using (var brush = new SolidBrush(Color.FromArgb(64, Color.Black)))
                    graphics.FillPath(brush, path);

                graphics.DrawString(
                    "+", SystemFonts.MenuFont, 
                    ((GetState() & RenderState.Hover) != 0) ? Brushes.White : Brushes.Black, boundary, 
                    GraphConstants.CenterTextStringFormat);
            }
        }
    }
    #endregion

    #region Node Creation

    //
    /////////////////////////////////////////////////////////////////////////////////////
    //
    //          Creating nodes & tag tags
    //  

    public class ShaderFragmentNodeTag
    {
        public string ArchiveName { get; set; }
        public UInt32 Id { get; set; }
        public ShaderFragmentNodeTag(string archiveName) { ArchiveName = archiveName; Id = ++nodeAccumulatingId; }
        private static UInt32 nodeAccumulatingId = 1;
    }

    public class ShaderProcedureNodeTag : ShaderFragmentNodeTag
    {
        public ProcedureNodeType Type { get; set; }
        public ShaderProcedureNodeTag(string archiveName) : base(archiveName) { Type = ProcedureNodeType.Normal; }
    }

    public class ShaderCapturesNodeTag : ShaderFragmentNodeTag
    {
        public ShaderCapturesNodeTag(string captureGroupName) : base(captureGroupName) { }
    }

    public class ShaderSubGraphNodeTag
    {
        public string Implements;
    }

    [Export(typeof(ShaderFragmentNodeCreator))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class ShaderFragmentNodeCreator
    {
        internal static IDictionary<Enum, string> ParamSourceTypeNames;
        
        static ShaderFragmentNodeCreator()
        {
            ParamSourceTypeNames = new Dictionary<Enum, string>
            {
                { ParamSourceType.System, "Input" },
                { ParamSourceType.Output, "Output" }
            };
        }

        private static System.Text.RegularExpressions.Regex s_templatedNameMatch = new System.Text.RegularExpressions.Regex(@"(.*)<(.+)>");

        private string VisibleName(string archiveName)
        {
            var complexMatch = s_templatedNameMatch.Match(archiveName);
            if (complexMatch.Success)
            {
                return VisibleName(complexMatch.Groups[2].Value);
            }
            else
            {
                int i = archiveName.LastIndexOf(':');
                if (i > 0) return archiveName.Substring(i + 1);
                return archiveName;
            }
        }

        private GUILayer.NodeGraphSignature FindSignature(GUILayer.NodeGraphFile documentContext, String name)
        {
            GUILayer.NodeGraphFile.SubGraph sg;
            if (documentContext != null && documentContext.SubGraphs.TryGetValue(name, out sg))
            {
                return sg.Signature;
            }
            return _shaderFragments.GetFunction(name, (documentContext != null) ? documentContext.GetSearchRules() : null);
        }

        public Node CreateProcedureNode(GUILayer.NodeGraphFile diagramContext, string archiveName, ProcedureNodeType type, IEnumerable<object> dataPackets)
        {
            var node = new Node { Title = VisibleName(archiveName) };
            node.Tag = new ShaderProcedureNodeTag(archiveName) { Type = type };
            node.Layout = Node.LayoutType.Rectangular;

            SetProcedureNodeType_Internal(diagramContext, node, type, dataPackets);
            UpdateProcedureNode(diagramContext, node);
            return node;
        }

        private void SetProcedureNodeType_Internal(GUILayer.NodeGraphFile diagramContext, Node node, ProcedureNodeType type, IEnumerable<object> dataPackets)
        {
            ShaderProcedureNodeTag tag = node.Tag as ShaderProcedureNodeTag;
            if (tag == null) return;

            // we should remove all items from the center except the title
            foreach (var item in node.CenterItems.ToList())
                if (item != node.TitleItem)
                    node.RemoveItem(item);
            foreach (var item in node.BottomItems.ToList())
                node.RemoveItem(item);

            foreach (var amender in Amenders)
                amender.AmendNode(diagramContext, node, type, dataPackets);

            tag.Type = type;
        }

        public void SetProcedureNodeType(GUILayer.NodeGraphFile diagramContext, Node node, ProcedureNodeType type)
        {
            SetProcedureNodeType_Internal(diagramContext, node, type, null);
            UpdateProcedureNode(diagramContext, node);
        }

        public ProcedureNodeType GetProcedureNodeType(Node node)
        {
            ShaderProcedureNodeTag tag = node.Tag as ShaderProcedureNodeTag;
            if (tag == null) return ProcedureNodeType.Normal;
            return tag.Type;
        }

        private void UpdateProcedureNodeDock(Node node, GUILayer.NodeGraphSignature signature, InterfaceDirection interfaceDirection)
        {
            bool isSubGraph = (node.Tag is ShaderSubGraphNodeTag);
            Node.Dock dock;
            if (isSubGraph)
            {
                // dock is flipped in the subgraph case
                dock = ShaderFragmentNodeConnector.GetDirectionalColumn(interfaceDirection == InterfaceDirection.In ? InterfaceDirection.Out : InterfaceDirection.In);
            }
            else
            {
                dock = ShaderFragmentNodeConnector.GetDirectionalColumn(interfaceDirection);
            }
            var paramDir = ShaderFragmentNodeConnector.AsParameterDirection(interfaceDirection);

            var procTag = node.Tag as ShaderProcedureNodeTag;
            if (procTag != null && procTag.Type == ProcedureNodeType.Instantiation)
            {
                if (interfaceDirection == InterfaceDirection.Out)
                {
                    bool foundInstantiationNode = false;
                    foreach (var c in node.OutputItems.ToList())
                    {
                        var conn = c as ShaderFragmentNodeConnector;
                        if (conn != null && string.Compare(conn.Name, "<instantiation>") == 0)
                        {
                            foundInstantiationNode = true;
                        }
                        else
                        {
                            node.RemoveItem(c);
                        }
                    }

                    if (!foundInstantiationNode)
                    {
                        var item = new ShaderFragmentNodeConnector("<instantiation>", "graph");
                        node.AddItem(item, dock);
                        node.MoveItemToTop(item);
                    }

                    return; // don't add output parameters from the signature
                }
            }

            // Remove any existing parameters that don't have connections. 
            // Items with connections we'll keep around, since if they don't match exactly
            // the names in the up-to-date underlying shader, we can't automatically migrate it across
            foreach (var item in node.ItemsForDock(dock).ToList())
            {
                var conn = item as NodeConnector;
                if (conn != null && !conn.HasConnection)
                    node.RemoveItem(conn);
            }

            var existingItems = node.ItemsForDock(dock);

            if ((!isSubGraph) && (interfaceDirection == InterfaceDirection.In)) // (don't do this for subgraphs)
            {
                foreach (var param in signature.TemplateParameters)
                {
                    var type = "graph";
                    if (!string.IsNullOrEmpty(param.Restriction))
                        type = "graph<" + param.Restriction + ">";

                    var existing = existingItems.Where(x =>
                    {
                        var conn = x as ShaderFragmentNodeConnector;
                        if (conn != null) return string.Compare(conn.Name, param.Name) == 0;
                        return false;
                    }).FirstOrDefault() as ShaderFragmentNodeConnector;

                    if (existing != null)
                    {
                        // not a lot of type checking for template parameters, so just reset the type directly
                        existing.Type = type;
                    }
                    else
                    {
                        existing = new ShaderFragmentNodeConnector(param.Name, type);
                        node.AddItem(existing, dock);
                    }
                    node.MoveItemToTop(existing);
                }
            }

            for (int p = signature.Parameters.Count - 1; p >= 0; --p)
            {
                var param = signature.Parameters[p];
                if (param.Direction != paramDir) continue;

                var existing = existingItems.Where(x =>
                {
                    var conn = x as ShaderFragmentNodeConnector;
                    if (conn != null) return string.Compare(conn.Name, param.Name) == 0;
                    return false;
                }).FirstOrDefault() as ShaderFragmentNodeConnector;

                // If the types don't agree, then we change the old one into a "type mismatch" mode, and 
                // create a new connector for this parameter
                if (existing != null)
                {
                    if (string.Compare(existing.Type, param.Type) != 0
                        && string.Compare(existing.Type, "auto") != 0
                        && string.Compare(param.Type, "auto") != 0)
                    {
                        existing.Name += "-typemismatch";
                        existing = null;
                    }
                }

                if (existing == null)
                {
                    if (isSubGraph)
                    {
                        existing = new ShaderFragmentInterfaceParameterItem(param.Name, param.Type);
                    }
                    else
                    {
                        existing = new ShaderFragmentNodeConnector(param.Name, param.Type);
                    }
                    
                    node.AddItem(existing, dock);
                }

                node.MoveItemToTop(existing);
            }

            // Any parameters left beyond this point are potentially now out-of-date. We'll 
            // leave them there
        }

        public void UpdateProcedureNode(GUILayer.NodeGraphFile diagramContext, Node node)
        {
            ShaderProcedureNodeTag tag = node.Tag as ShaderProcedureNodeTag;
            if (tag == null) return;

            var archiveNameForSignature = tag.ArchiveName;
            var templatedName = s_templatedNameMatch.Match(archiveNameForSignature);
            if (templatedName.Success)
                archiveNameForSignature = templatedName.Groups[2].Value;

            // If we find a signature, we should update the inputs and outputs
            // to match that signature. We will reorder the connectors we find, and
            // any connectors we don't find will end up on the bottom of the list.

            var signature = FindSignature(diagramContext, archiveNameForSignature);
            if (signature != null)
            {
                UpdateProcedureNodeDock(node, signature, InterfaceDirection.In);
                UpdateProcedureNodeDock(node, signature, InterfaceDirection.Out);
            }

            // If we're an instantiation node, and we're connected via our instantiation
            // connector, then we must filter out some of the inputs
            if (tag.Type == ProcedureNodeType.Instantiation)
            {
                var existingInstantiation = node.OutputItems.Where(x =>
                {
                    var conn = x as ShaderFragmentNodeConnector;
                    if (conn != null) return string.Compare(conn.Name, "<instantiation>") == 0;
                    return false;
                }).FirstOrDefault() as ShaderFragmentNodeConnector;

                if (existingInstantiation != null && existingInstantiation.Connectors.FirstOrDefault() != null)
                {
                    HashSet<String> filteredParams = new HashSet<String>();
                    foreach (ShaderFragmentNodeConnector inCon in node.InputConnectors)
                        filteredParams.Add(inCon.Name);
                    bool foundAtLeastOne = false;
                    foreach(var connection in existingInstantiation.Connectors)
                    {
                        var target = connection.To as ShaderFragmentNodeConnector;
                        if (target == null) continue;

                        var match = s_templatedNameMatch.Match(target.Type);
                        if (!match.Success) continue;

                        var restrictionSig = FindSignature(diagramContext, match.Groups[2].Value);
                        if (restrictionSig == null) continue;

                        HashSet<String> filtering = new HashSet<String>();
                        foreach (var param in restrictionSig.Parameters)
                            if (param.Direction == GUILayer.NodeGraphSignature.ParameterDirection.In)
                                filtering.Add(param.Name);

                        filteredParams.UnionWith(filtering);
                        foundAtLeastOne = true;
                    }

                    // We're left with every parameter that is in every connected 
                    if (foundAtLeastOne)
                    {
                        foreach (var c in node.InputConnectors.ToList())    // (clone because doing erase operations below)
                        {
                            var conn = c as ShaderFragmentNodeConnector;
                            if (conn != null && filteredParams.Contains(conn.Name))
                                node.RemoveItem(conn);
                        }
                    }
                }
            }
        }

        private static void ParameterNodeTypeChanged(object sender, HyperGraph.Items.AcceptNodeSelectionChangedEventArgs args)
        {
            if (sender is HyperGraph.Items.NodeDropDownItem)
            {
                var item = (HyperGraph.Items.NodeDropDownItem)sender;
                var node = item.Node;
                var newType = Utils.AsEnumValue<ParamSourceType>(item.Items[args.Index], ParamSourceTypeNames);

                    //  We might have to change the input/output settings on this node
                bool isInput = newType != ParamSourceType.Output;
                var oldColumn = ShaderFragmentNodeConnector.GetDirectionalColumn(isInput ? InterfaceDirection.Out : InterfaceDirection.In);
                var newColumn = ShaderFragmentNodeConnector.GetDirectionalColumn(isInput ? InterfaceDirection.In : InterfaceDirection.Out);
                var oldItems = new List<HyperGraph.NodeItem>(node.ItemsForDock(oldColumn));
                foreach (var i in oldItems)
                {
                    if (i is ShaderFragmentNodeConnector)
                    {
                                // if this is a node item with exactly 1 input/output
                                // and it is not in the correct direction, then we have to change it.
                                //  we can't change directly. We need to delete and recreate this node item.
                        var fragItem = (ShaderFragmentNodeConnector)i;
                        node.RemoveItem(fragItem);
                        node.AddItem(
                            new ShaderFragmentNodeConnector(fragItem.Name, fragItem.Type),
                            newColumn);
                    }
                }
            }
        }

        public Node CreateCapturesNode(String name, IEnumerable<GUILayer.NodeGraphSignature.Parameter> parameters)
        {
            var node = new Node { Title = name };
            node.Tag = new ShaderCapturesNodeTag(name);

            foreach (var param in parameters)
            {
                node.AddItem(
                    new ShaderFragmentCaptureParameterItem(param.Name, param.Type) { Semantic = param.Semantic, Default = param.Default, Direction = InterfaceDirection.Out },
                    Node.Dock.Output);
            }

            node.AddItem(
                new ShaderFragmentAddParameterItem { ConnectorCreator = (n, t) => new ShaderFragmentCaptureParameterItem(n, t) { Direction = InterfaceDirection.Out } }, 
                Node.Dock.Output);

            return node;
        }

        public Node CreateSubGraph(GUILayer.NodeGraphFile context, String name, String implements)
        {
            var node = new Node { };
            var tag = new ShaderSubGraphNodeTag { Implements = implements };
            node.Tag = tag;
            node.SubGraphTag = name;

            var title = name;
            if (!String.IsNullOrEmpty(implements))
                title += " [implements " + implements + "]";
            node.AddItem(new HyperGraph.Items.NodeTitleItem { Title = title }, Node.Dock.Top);            
            UpdateSubGraphNode(context, node);
            return node;
        }

        public void SetSubGraphProperties(GUILayer.NodeGraphFile context, Node subGraph, String name, String implements)
        {
            var tag = subGraph.Tag as ShaderSubGraphNodeTag;
            if (tag == null) return;

            tag.Implements = implements;
            var title = subGraph.TopItems.ElementAt(0) as HyperGraph.Items.NodeTitleItem;
            if (title != null)
            {
                title.Title = name;
                if (!String.IsNullOrEmpty(implements))
                    title.Title += " implements " + implements;
            }

            UpdateSubGraphNode(context, subGraph);
        }

        public void GetSubGraphProperties(Node subGraph, out String name, out String implements)
        {
            var tag = subGraph.Tag as ShaderSubGraphNodeTag;
            if (tag == null)
            {
                name = implements = null;
                return;
            }

            name = subGraph.SubGraphTag as string;
            implements = tag.Implements;
        }

        public void UpdateSubGraphNode(GUILayer.NodeGraphFile context, Node subGraph)
        {
            ShaderSubGraphNodeTag tag = subGraph.Tag as ShaderSubGraphNodeTag;
            if (tag == null) return;

            var archiveNameForImplements = tag.Implements;

            var signature = FindSignature(context, archiveNameForImplements);
            if (signature != null)
            {
                UpdateProcedureNodeDock(subGraph, signature, InterfaceDirection.In);
                UpdateProcedureNodeDock(subGraph, signature, InterfaceDirection.Out);
            }
        }

        public Node FindNodeFromId(HyperGraph.IGraphModel graph, UInt64 id)
        {
            foreach (Node n in graph.Nodes)
            {
                if (n.Tag is ShaderFragmentNodeTag
                    && ((ShaderFragmentNodeTag)n.Tag).Id == (UInt64)id)
                {
                    return n;
                }
            }
            return null;
        }

        public bool IsInstantiationConnector(HyperGraph.NodeConnector connector)
        {
            if (connector is ShaderFragmentNodeConnector nodeConnector)
            {
                return string.Equals(nodeConnector.Name, "<instantiation>");
            }
            return false;
        }

        private bool IsInputConnector(NodeConnector connector) => connector.Node.InputConnectors.Contains(connector);

        public string GetDescription(object o)
        {
            var node = o as Node;
            if (node != null)
            {
                return node.Title;
            }

            var item = o as ShaderFragmentNodeConnector;
            if (item != null)
            {
                string result;
                if (IsInputConnector(item)) {
                    result = "Input [";
                } else {
                    result = "Output [";
                }
                result += item.GetNameText() + " (" + item.Type + ")]";
                foreach (var c in item.Connectors)
                {
                    var t = (c.From != null) ? (c.From as ShaderFragmentNodeConnector) : null;
                    if (t != null)
                    {
                        result += " <===> [" + t.GetNameText() + "] in " + t.Node.Title;
                    }
                    else
                        result += " <===> " + c.Text;
                }

                return result;
            }

            var preview = o as ShaderFragmentPreviewItem;
            if (preview != null)
                return "preview";

            var dropDownList = o as HyperGraph.Items.NodeDropDownItem;
            if (dropDownList != null)
                return dropDownList.Items[dropDownList.SelectedIndex].ToString();

            var textBox = o as HyperGraph.Items.NodeTextBoxItem;
            if (textBox != null)
                return textBox.Text;

            return string.Empty;
        }

        public HyperGraph.Compatibility.ICompatibilityStrategy CreateCompatibilityStrategy()
        {
            return new ShaderFragmentNodeCompatibility();
        }

        [Import]
        Archive _shaderFragments;

        [ImportMany]
        IEnumerable<INodeAmender> Amenders;
    }

    #endregion

}
