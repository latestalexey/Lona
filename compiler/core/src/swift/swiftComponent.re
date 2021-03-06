module Format = SwiftFormat;

module Ast = SwiftAst;

module Document = SwiftDocument;

module Render = SwiftRender;

type layoutPriority =
  | Required
  | Low;

type constraintDefinition = {
  variableName: string,
  initialValue: Ast.node,
  priority: layoutPriority
};

type directionParameter = {
  lonaName: string,
  swiftName: string
};

let generate =
    (
      options: Options.options,
      swiftOptions: SwiftOptions.options,
      name,
      colors,
      textStyles: TextStyle.file,
      json
    ) => {
  let rootLayer = json |> Decode.Component.rootLayer;
  /* Remove the root element */
  let nonRootLayers = rootLayer |> Layer.flatten |> List.tl;
  let logic = json |> Decode.Component.logic;
  let logic =
    Logic.enforceSingleAssignment(
      (_, path) => [
        "_" ++ Format.variableNameFromIdentifier(rootLayer.name, path)
      ],
      (_, path) =>
        Logic.Literal(LonaValue.defaultValueForParameter(List.nth(path, 2))),
      logic
    );
  let layerParameterAssignments =
    Layer.logicAssignmentsFromLayerParameters(rootLayer);
  let assignments = Layer.parameterAssignmentsFromLogic(rootLayer, logic);
  let parameters = json |> Decode.Component.parameters;
  open Ast;
  let priorityName =
    fun
    | Required => "required"
    | Low => "defaultLow";
  let typeAnnotationDoc =
    fun
    | Types.Reference(typeName) =>
      switch typeName {
      | "Boolean" => TypeName("Bool")
      | _ => TypeName(typeName)
      }
    | Named(name, _) => TypeName(name);
  let parameterVariableDoc = (parameter: Decode.parameter) =>
    VariableDeclaration({
      "modifiers": [AccessLevelModifier(PublicModifier)],
      "pattern":
        IdentifierPattern({
          "identifier": SwiftIdentifier(parameter.name),
          "annotation": Some(parameter.ltype |> typeAnnotationDoc)
        }),
      "init": None,
      "block":
        Some(
          WillSetDidSetBlock({
            "willSet": None,
            "didSet":
              Some([
                FunctionCallExpression({
                  "name": SwiftIdentifier("update"),
                  "arguments": []
                })
              ])
          })
        )
    });
  let getLayerTypeName = layerType =>
    switch (swiftOptions.framework, layerType) {
    | (UIKit, Types.View) => "UIView"
    | (UIKit, Text) => "UILabel"
    | (UIKit, Image) => "UIImageView"
    | (AppKit, Types.View) => "NSBox"
    | (AppKit, Text) => "NSTextField"
    | (AppKit, Image) => "NSImageView"
    | _ => "TypeUnknown"
    };
  let getLayerInitCall = layerType => {
    let typeName = SwiftIdentifier(layerType |> getLayerTypeName);
    switch (swiftOptions.framework, layerType) {
    | (UIKit, Types.View)
    | (UIKit, Image) =>
      FunctionCallExpression({
        "name": typeName,
        "arguments": [
          FunctionCallArgument({
            "name": Some(SwiftIdentifier("frame")),
            "value": SwiftIdentifier(".zero")
          })
        ]
      })
    | (AppKit, Text) =>
      FunctionCallExpression({
        "name": typeName,
        "arguments": [
          FunctionCallArgument({
            "name": Some(SwiftIdentifier("labelWithString")),
            "value": LiteralExpression(String(""))
          })
        ]
      })
    | _ => FunctionCallExpression({"name": typeName, "arguments": []})
    };
  };
  let viewVariableDoc = (layer: Types.layer) =>
    VariableDeclaration({
      "modifiers": [AccessLevelModifier(PrivateModifier)],
      "pattern":
        IdentifierPattern({
          "identifier": SwiftIdentifier(layer.name |> Format.layerName),
          "annotation": None /*Some(layer.typeName |> viewTypeDoc)*/
        }),
      "init": Some(getLayerInitCall(layer.typeName)),
      "block": None
    });
  let textStyleVariableDoc = (layer: Types.layer) =>
    VariableDeclaration({
      "modifiers": [AccessLevelModifier(PrivateModifier)],
      "pattern":
        IdentifierPattern({
          "identifier":
            SwiftIdentifier((layer.name |> Format.layerName) ++ "TextStyle"),
          "annotation": None /* Some(TypeName("AttributedFont")) */
        }),
      "init":
        Some(
          MemberExpression([
            SwiftIdentifier("TextStyles"),
            SwiftIdentifier(textStyles.defaultStyle.id)
          ])
        ),
      "block": None
    });
  let constraintVariableDoc = variableName =>
    VariableDeclaration({
      "modifiers": [AccessLevelModifier(PrivateModifier)],
      "pattern":
        IdentifierPattern({
          "identifier": SwiftIdentifier(variableName),
          "annotation": Some(OptionalType(TypeName("NSLayoutConstraint")))
        }),
      "init": None,
      "block": None
    });
  let paddingParameters = [
    {swiftName: "topPadding", lonaName: "paddingTop"},
    {swiftName: "trailingPadding", lonaName: "paddingRight"},
    {swiftName: "bottomPadding", lonaName: "paddingBottom"},
    {swiftName: "leadingPadding", lonaName: "paddingLeft"}
  ];
  let marginParameters = [
    {swiftName: "topMargin", lonaName: "marginTop"},
    {swiftName: "trailingMargin", lonaName: "marginRight"},
    {swiftName: "bottomMargin", lonaName: "marginBottom"},
    {swiftName: "leadingMargin", lonaName: "marginLeft"}
  ];
  let spacingVariableDoc = (layer: Types.layer) => {
    let variableName = variable =>
      layer === rootLayer ?
        variable : Format.layerName(layer.name) ++ Format.upperFirst(variable);
    let marginVariables =
      layer === rootLayer ?
        [] :
        {
          let createVariable = (marginParameter: directionParameter) =>
            VariableDeclaration({
              "modifiers": [AccessLevelModifier(PrivateModifier)],
              "pattern":
                IdentifierPattern({
                  "identifier":
                    SwiftIdentifier(variableName(marginParameter.swiftName)),
                  "annotation": Some(TypeName("CGFloat"))
                }),
              "init":
                Some(
                  LiteralExpression(
                    FloatingPoint(
                      Layer.getNumberParameter(marginParameter.lonaName, layer)
                    )
                  )
                ),
              "block": None
            });
          marginParameters |> List.map(createVariable);
        };
    let paddingVariables =
      switch layer.children {
      | [] => []
      | _ =>
        let createVariable = (paddingParameter: directionParameter) =>
          VariableDeclaration({
            "modifiers": [AccessLevelModifier(PrivateModifier)],
            "pattern":
              IdentifierPattern({
                "identifier":
                  SwiftIdentifier(variableName(paddingParameter.swiftName)),
                "annotation": Some(TypeName("CGFloat"))
              }),
            "init":
              Some(
                LiteralExpression(
                  FloatingPoint(
                    Layer.getNumberParameter(paddingParameter.lonaName, layer)
                  )
                )
              ),
            "block": None
          });
        paddingParameters |> List.map(createVariable);
      };
    marginVariables @ paddingVariables;
  };
  let initParameterDoc = (parameter: Decode.parameter) =>
    Parameter({
      "externalName": None,
      "localName": parameter.name,
      "annotation": parameter.ltype |> typeAnnotationDoc,
      "defaultValue": None
    });
  let initParameterAssignmentDoc = (parameter: Decode.parameter) =>
    BinaryExpression({
      "left":
        MemberExpression([
          SwiftIdentifier("self"),
          SwiftIdentifier(parameter.name)
        ]),
      "operator": "=",
      "right": SwiftIdentifier(parameter.name)
    });
  let initializerCoderDoc = () =>
    /* required init?(coder aDecoder: NSCoder) {
         fatalError("init(coder:) has not been implemented")
       } */
    InitializerDeclaration({
      "modifiers": [AccessLevelModifier(PublicModifier), RequiredModifier],
      "parameters": [
        Parameter({
          "externalName": Some("coder"),
          "localName": "aDecoder",
          "annotation": TypeName("NSCoder"),
          "defaultValue": None
        })
      ],
      "failable": Some("?"),
      "body": [
        FunctionCallExpression({
          "name": SwiftIdentifier("fatalError"),
          "arguments": [
            FunctionCallArgument({
              "name": None,
              "value":
                SwiftIdentifier("\"init(coder:) has not been implemented\"")
            })
          ]
        })
      ]
    });
  let initializerDoc = () =>
    InitializerDeclaration({
      "modifiers": [AccessLevelModifier(PublicModifier)],
      "parameters": parameters |> List.map(initParameterDoc),
      "failable": None,
      "body":
        Document.joinGroups(
          Empty,
          [
            parameters |> List.map(initParameterAssignmentDoc),
            [
              MemberExpression([
                SwiftIdentifier("super"),
                FunctionCallExpression({
                  "name": SwiftIdentifier("init"),
                  "arguments": [
                    FunctionCallArgument({
                      "name": Some(SwiftIdentifier("frame")),
                      "value": SwiftIdentifier(".zero")
                    })
                  ]
                })
              ])
            ],
            [
              FunctionCallExpression({
                "name": SwiftIdentifier("setUpViews"),
                "arguments": []
              }),
              FunctionCallExpression({
                "name": SwiftIdentifier("setUpConstraints"),
                "arguments": []
              })
            ],
            [
              FunctionCallExpression({
                "name": SwiftIdentifier("update"),
                "arguments": []
              })
            ]
          ]
        )
    });
  let memberOrSelfExpression = (firstIdentifier, statements) =>
    switch firstIdentifier {
    | "self" => MemberExpression(statements)
    | _ => MemberExpression([SwiftIdentifier(firstIdentifier)] @ statements)
    };
  let parentNameOrSelf = (parent: Types.layer) =>
    parent === rootLayer ? "self" : parent.name |> Format.layerName;
  let layerMemberExpression = (layer: Types.layer, statements) =>
    memberOrSelfExpression(parentNameOrSelf(layer), statements);
  let defaultValueForParameter =
    fun
    | "backgroundColor" =>
      MemberExpression([SwiftIdentifier("UIColor"), SwiftIdentifier("clear")])
    | "font"
    | "textStyle" =>
      MemberExpression([
        SwiftIdentifier("TextStyles"),
        SwiftIdentifier(textStyles.defaultStyle.id)
      ])
    | _ => LiteralExpression(Integer(0));
  let defineInitialLayerValue = (layer: Types.layer, (name, _)) => {
    let parameters = Layer.LayerMap.find_opt(layer, layerParameterAssignments);
    switch parameters {
    | None => LineComment(layer.name)
    | Some(parameters) =>
      let assignment = StringMap.find_opt(name, parameters);
      let logic =
        switch assignment {
        | None =>
          Logic.defaultAssignmentForLayerParameter(
            colors,
            textStyles,
            layer,
            name
          )
        | Some(assignment) => assignment
        };
      let node =
        SwiftLogic.toSwiftAST(
          swiftOptions,
          colors,
          textStyles,
          rootLayer,
          logic
        );
      StatementListHelper(node);
    };
  };
  let setUpViewsDoc = (root: Types.layer) => {
    let setUpDefaultsDoc = () => {
      let filterParameters = ((name, _)) =>
        name != "flexDirection"
        && name != "justifyContent"
        && name != "alignSelf"
        && name != "alignItems"
        && name != "flex"
        /* && name != "font" */
        && ! Js.String.startsWith("padding", name)
        && ! Js.String.startsWith("margin", name)
        /* Handled by initial constraint setup */
        && name != "height"
        && name != "width";
      let filterNotAssignedByLogic = (layer: Types.layer, (parameterName, _)) =>
        switch (Layer.LayerMap.find_opt(layer, assignments)) {
        | None => true
        | Some(parameters) =>
          switch (StringMap.find_opt(parameterName, parameters)) {
          | None => true
          | Some(_) => false
          }
        };
      let defineInitialLayerValues = (layer: Types.layer) =>
        layer.parameters
        |> StringMap.bindings
        |> List.filter(filterParameters)
        |> List.filter(filterNotAssignedByLogic(layer))
        |> List.map(((k, v)) => defineInitialLayerValue(layer, (k, v)));
      rootLayer
      |> Layer.flatten
      |> List.map(defineInitialLayerValues)
      |> List.concat;
    };
    let resetViewStyling = (layer: Types.layer) =>
      switch layer.typeName {
      | View => [
          BinaryExpression({
            "left": layerMemberExpression(layer, [SwiftIdentifier("boxType")]),
            "operator": "=",
            "right": SwiftIdentifier(".custom")
          }),
          BinaryExpression({
            "left":
              layerMemberExpression(layer, [SwiftIdentifier("borderType")]),
            "operator": "=",
            "right": SwiftIdentifier(".noBorder")
          }),
          BinaryExpression({
            "left":
              layerMemberExpression(
                layer,
                [SwiftIdentifier("contentViewMargins")]
              ),
            "operator": "=",
            "right": SwiftIdentifier(".zero")
          })
        ]
      | Text => [
          BinaryExpression({
            "left":
              layerMemberExpression(layer, [SwiftIdentifier("lineBreakMode")]),
            "operator": "=",
            "right": SwiftIdentifier(".byWordWrapping")
          })
        ]
      | _ => []
      };
    let addSubviews = (parent: option(Types.layer), layer: Types.layer) =>
      switch parent {
      | None => []
      | Some(parent) => [
          FunctionCallExpression({
            "name":
              layerMemberExpression(parent, [SwiftIdentifier("addSubview")]),
            "arguments": [SwiftIdentifier(layer.name |> Format.layerName)]
          })
        ]
      };
    FunctionDeclaration({
      "name": "setUpViews",
      "modifiers": [AccessLevelModifier(PrivateModifier)],
      "parameters": [],
      "result": None,
      "body":
        Document.joinGroups(
          Empty,
          [
            swiftOptions.framework == SwiftOptions.AppKit ?
              Layer.flatmap(resetViewStyling, root) |> List.concat : [],
            Layer.flatmapParent(addSubviews, root) |> List.concat,
            setUpDefaultsDoc()
          ]
        )
    });
  };
  let getConstraints = (root: Types.layer) => {
    let setUpContraint =
        (
          layer: Types.layer,
          anchor1,
          parent: Types.layer,
          anchor2,
          relation,
          value,
          suffix
        ) => {
      let variableName =
        (
          layer === rootLayer ?
            anchor1 :
            Format.layerName(layer.name) ++ Format.upperFirst(anchor1)
        )
        ++ suffix;
      let initialValue =
        MemberExpression([
          SwiftIdentifier(layer.name |> Format.layerName),
          SwiftIdentifier(anchor1),
          FunctionCallExpression({
            "name": SwiftIdentifier("constraint"),
            "arguments": [
              FunctionCallArgument({
                "name": Some(SwiftIdentifier(relation)),
                "value":
                  layerMemberExpression(parent, [SwiftIdentifier(anchor2)])
              }),
              FunctionCallArgument({
                "name": Some(SwiftIdentifier("constant")),
                "value": value
              })
            ]
          })
        ]);
      {variableName, initialValue, priority: Required};
    };
    let setUpLessThanOrEqualToContraint =
        (
          layer: Types.layer,
          anchor1,
          parent: Types.layer,
          anchor2,
          value,
          suffix
        ) => {
      let variableName =
        (
          layer === rootLayer ?
            anchor1 :
            Format.layerName(layer.name) ++ Format.upperFirst(anchor1)
        )
        ++ suffix;
      let initialValue =
        MemberExpression([
          SwiftIdentifier(layer.name |> Format.layerName),
          SwiftIdentifier(anchor1),
          FunctionCallExpression({
            "name": SwiftIdentifier("constraint"),
            "arguments": [
              FunctionCallArgument({
                "name": Some(SwiftIdentifier("lessThanOrEqualTo")),
                "value":
                  layerMemberExpression(parent, [SwiftIdentifier(anchor2)])
              }),
              FunctionCallArgument({
                "name": Some(SwiftIdentifier("constant")),
                "value": value
              })
            ]
          })
        ]);
      {variableName, initialValue, priority: Low};
    };
    let setUpDimensionContraint = (layer: Types.layer, anchor, constant) => {
      let variableName =
        (
          layer === rootLayer ?
            anchor : Format.layerName(layer.name) ++ Format.upperFirst(anchor)
        )
        ++ "Constraint";
      let initialValue =
        layerMemberExpression(
          layer,
          [
            SwiftIdentifier(anchor),
            FunctionCallExpression({
              "name": SwiftIdentifier("constraint"),
              "arguments": [
                FunctionCallArgument({
                  "name": Some(SwiftIdentifier("equalToConstant")),
                  "value": LiteralExpression(FloatingPoint(constant))
                })
              ]
            })
          ]
        );
      {variableName, initialValue, priority: Required};
    };
    let negateNumber = expression =>
      PrefixExpression({"operator": "-", "expression": expression});
    let constraintConstantExpression =
        (layer: Types.layer, variable1, parent: Types.layer, variable2) => {
      let variableName = (layer: Types.layer, variable) =>
        layer === rootLayer ?
          variable :
          Format.layerName(layer.name) ++ Format.upperFirst(variable);
      BinaryExpression({
        "left": SwiftIdentifier(variableName(layer, variable1)),
        "operator": "+",
        "right": SwiftIdentifier(variableName(parent, variable2))
      });
    };
    let constrainAxes = (layer: Types.layer) => {
      let direction = Layer.getFlexDirection(layer);
      let primaryBeforeAnchor =
        direction == "column" ? "topAnchor" : "leadingAnchor";
      let primaryAfterAnchor =
        direction == "column" ? "bottomAnchor" : "trailingAnchor";
      let secondaryBeforeAnchor =
        direction == "column" ? "leadingAnchor" : "topAnchor";
      let secondaryAfterAnchor =
        direction == "column" ? "trailingAnchor" : "bottomAnchor";
      let height = Layer.getNumberParameterOpt("height", layer);
      let width = Layer.getNumberParameterOpt("width", layer);
      let primaryDimension = direction == "column" ? "height" : "width";
      let secondaryDimension = direction == "column" ? "width" : "height";
      let secondaryDimensionAnchor = secondaryDimension ++ "Anchor";
      /* let primaryDimensionValue = direction == "column" ? height : width; */
      /* let secondaryDimensionValue = direction == "column" ? width : height; */
      let sizingRules =
        layer |> Layer.getSizingRules(Layer.findParent(rootLayer, layer));
      let primarySizingRule =
        direction == "column" ? sizingRules.height : sizingRules.width;
      let secondarySizingRule =
        direction == "column" ? sizingRules.width : sizingRules.height;
      let flexChildren =
        layer.children
        |> List.filter((child: Types.layer) =>
             Layer.getNumberParameter("flex", child) === 1.0
           );
      let addConstraints = (index, child: Types.layer) => {
        let childSizingRules = child |> Layer.getSizingRules(Some(layer));
        /* let childPrimarySizingRule =
           direction == "column" ? childSizingRules.height : childSizingRules.width; */
        let childSecondarySizingRule =
          direction == "column" ?
            childSizingRules.width : childSizingRules.height;
        let firstViewConstraints =
          switch index {
          | 0 =>
            let primaryBeforeConstant =
              direction == "column" ?
                constraintConstantExpression(
                  layer,
                  "topPadding",
                  child,
                  "topMargin"
                ) :
                constraintConstantExpression(
                  layer,
                  "leadingPadding",
                  child,
                  "leadingMargin"
                );
            [
              setUpContraint(
                child,
                primaryBeforeAnchor,
                layer,
                primaryBeforeAnchor,
                "equalTo",
                primaryBeforeConstant,
                "Constraint"
              )
            ];
          | _ => []
          };
        let lastViewConstraints =
          switch index {
          | x when x == List.length(layer.children) - 1 =>
            /* If the parent view has a fixed dimension, we don't need to add a constraint...
               unless any child has "flex: 1", in which case we do still need the constraint. */
            let needsPrimaryAfterConstraint =
              switch (primarySizingRule, List.length(flexChildren)) {
              /* | (FitContent, _) => false */
              | (Fill, count) when count == 0 => false
              | (Fixed(_), count) when count == 0 => false
              | (_, _) => true
              };
            /* let needsPrimaryAfterConstraint =
               Layer.getNumberParameterOpt(primaryDimension, layer) == None
               || List.length(flexChildren) > 0; */
            let primaryAfterConstant =
              direction == "column" ?
                constraintConstantExpression(
                  layer,
                  "bottomPadding",
                  child,
                  "bottomMargin"
                ) :
                constraintConstantExpression(
                  layer,
                  "trailingPadding",
                  child,
                  "trailingMargin"
                );
            needsPrimaryAfterConstraint ?
              [
                setUpContraint(
                  child,
                  primaryAfterAnchor,
                  layer,
                  primaryAfterAnchor,
                  "equalTo",
                  negateNumber(primaryAfterConstant),
                  "Constraint"
                )
              ] :
              [];
          | _ => []
          };
        let middleViewConstraints =
          switch index {
          | 0 => []
          | _ =>
            let previousLayer = List.nth(layer.children, index - 1);
            let betweenConstant =
              direction == "column" ?
                constraintConstantExpression(
                  previousLayer,
                  "bottomMargin",
                  child,
                  "topMargin"
                ) :
                constraintConstantExpression(
                  previousLayer,
                  "trailingMargin",
                  child,
                  "leadingMargin"
                );
            [
              setUpContraint(
                child,
                primaryBeforeAnchor,
                previousLayer,
                primaryAfterAnchor,
                "equalTo",
                betweenConstant,
                "Constraint"
              )
            ];
          };
        let secondaryBeforeConstant =
          direction == "column" ?
            constraintConstantExpression(
              layer,
              "leadingPadding",
              child,
              "leadingMargin"
            ) :
            constraintConstantExpression(
              layer,
              "topPadding",
              child,
              "topMargin"
            );
        let secondaryAfterConstant =
          direction == "column" ?
            constraintConstantExpression(
              layer,
              "trailingPadding",
              child,
              "trailingMargin"
            ) :
            constraintConstantExpression(
              layer,
              "bottomPadding",
              child,
              "bottomMargin"
            );
        let secondaryBeforeConstraint =
          setUpContraint(
            child,
            secondaryBeforeAnchor,
            layer,
            secondaryBeforeAnchor,
            "equalTo",
            secondaryBeforeConstant,
            "Constraint"
          );
        let secondaryAfterConstraint =
          switch (secondarySizingRule, childSecondarySizingRule) {
          | (_, Fixed(_)) => [] /* Width/height constraints are added outside the child loop */
          | (_, Fill) => [
              setUpContraint(
                child,
                secondaryAfterAnchor,
                layer,
                secondaryAfterAnchor,
                "equalTo",
                negateNumber(secondaryAfterConstant),
                "Constraint"
              )
            ]
          | (Fill, FitContent) => [
              setUpContraint(
                child,
                secondaryAfterAnchor,
                layer,
                secondaryAfterAnchor,
                "lessThanOrEqualTo",
                negateNumber(secondaryAfterConstant),
                "Constraint"
              )
            ]
          | (_, FitContent) => [
              setUpContraint(
                child,
                secondaryAfterAnchor,
                layer,
                secondaryAfterAnchor,
                "equalTo",
                negateNumber(secondaryAfterConstant),
                "Constraint"
              )
            ]
          };
        /* If the parent's secondary axis is set to "fit content", this ensures
           the secondary axis dimension is greater than every child's.
           We apply these in the child loop for easier variable naming (due to current setup). */
        /*
           We need these constraints to be low priority. A "FitContent" view needs height >= each
           of its children. Yet a "Fill" sibling needs to have height unspecified, and
           a side anchor equal to the side of the "FitContent" view.
           This layout is ambiguous (I think), despite no warnings at runtime. The "FitContent" view's
           height constraints seem to take priority over the "Fill" view's height constraints, and the
           "FitContent" view steals the height of the "Fill" view. We solve this by lowering the priority
           of the "FitContent" view's height.
         */
        let fitContentSecondaryConstraint =
          switch secondarySizingRule {
          | FitContent => [
              setUpLessThanOrEqualToContraint(
                child,
                secondaryDimensionAnchor,
                layer,
                secondaryDimensionAnchor,
                negateNumber(
                  BinaryExpression({
                    "left": secondaryBeforeConstant,
                    "operator": "+",
                    "right": secondaryAfterConstant
                  })
                ),
                "ParentConstraint"
              )
            ]
          | _ => []
          };
        firstViewConstraints
        @ lastViewConstraints
        @ middleViewConstraints
        @ [secondaryBeforeConstraint]
        @ secondaryAfterConstraint
        @ fitContentSecondaryConstraint;
      };
      /* Children with "flex: 1" should all have equal dimensions along the primary axis */
      let flexChildrenConstraints =
        switch flexChildren {
        | [first, ...rest] when List.length(rest) > 0 =>
          let sameAnchor = primaryDimension ++ "Anchor";
          let sameAnchorConstraint = (anchor, index, layer) =>
            setUpContraint(
              first,
              anchor,
              layer,
              anchor,
              "equalTo",
              LiteralExpression(FloatingPoint(0.0)),
              "SiblingConstraint" ++ string_of_int(index)
            );
          rest |> List.mapi(sameAnchorConstraint(sameAnchor));
        | _ => []
        };
      let heightConstraint =
        switch height {
        | Some(height) => [
            setUpDimensionContraint(layer, "heightAnchor", height)
          ]
        | None => []
        };
      let widthConstraint =
        switch width {
        | Some(width) => [setUpDimensionContraint(layer, "widthAnchor", width)]
        | None => []
        };
      let constraints =
        [heightConstraint, widthConstraint]
        @ [flexChildrenConstraints]
        @ (layer.children |> List.mapi(addConstraints));
      constraints |> List.concat;
    };
    root |> Layer.flatmap(constrainAxes) |> List.concat;
  };
  let constraints = getConstraints(rootLayer);
  let setUpConstraintsDoc = (root: Types.layer) => {
    let translatesAutoresizingMask = (layer: Types.layer) =>
      BinaryExpression({
        "left":
          layerMemberExpression(
            layer,
            [SwiftIdentifier("translatesAutoresizingMaskIntoConstraints")]
          ),
        "operator": "=",
        "right": LiteralExpression(Boolean(false))
      });
    let defineConstraint = def =>
      ConstantDeclaration({
        "modifiers": [],
        "init": Some(def.initialValue),
        "pattern":
          IdentifierPattern({
            "identifier": SwiftIdentifier(def.variableName),
            "annotation": None
          })
      });
    let setConstraintPriority = def =>
      BinaryExpression({
        "left":
          MemberExpression([
            SwiftIdentifier(def.variableName),
            SwiftIdentifier("priority")
          ]),
        "operator": "=",
        "right":
          MemberExpression([
            SwiftDocument.layoutPriorityTypeDoc(swiftOptions.framework),
            SwiftIdentifier(priorityName(def.priority))
          ])
      });
    let activateConstraints = () =>
      FunctionCallExpression({
        "name":
          MemberExpression([
            SwiftIdentifier("NSLayoutConstraint"),
            SwiftIdentifier("activate")
          ]),
        "arguments": [
          FunctionCallArgument({
            "name": None,
            "value":
              LiteralExpression(
                Array(
                  constraints
                  |> List.map(def => SwiftIdentifier(def.variableName))
                )
              )
          })
        ]
      });
    let assignConstraint = def =>
      BinaryExpression({
        "left":
          MemberExpression([
            SwiftIdentifier("self"),
            SwiftIdentifier(def.variableName)
          ]),
        "operator": "=",
        "right": SwiftIdentifier(def.variableName)
      });
    let assignConstraintIdentifier = def =>
      BinaryExpression({
        "left":
          MemberExpression([
            SwiftIdentifier(def.variableName),
            SwiftIdentifier("identifier")
          ]),
        "operator": "=",
        "right": LiteralExpression(String(def.variableName))
      });
    FunctionDeclaration({
      "name": "setUpConstraints",
      "modifiers": [AccessLevelModifier(PrivateModifier)],
      "parameters": [],
      "result": None,
      "body":
        List.concat([
          root |> Layer.flatmap(translatesAutoresizingMask),
          [Empty],
          constraints |> List.map(defineConstraint),
          constraints
          |> List.filter(def => def.priority == Low)
          |> List.map(setConstraintPriority),
          [Empty],
          [activateConstraints()],
          [Empty],
          constraints |> List.map(assignConstraint),
          [Empty, LineComment("For debugging")],
          constraints |> List.map(assignConstraintIdentifier)
        ])
    });
  };
  let updateDoc = () => {
    /* let printStringBinding = ((key, value)) => Js.log2(key, value);
       let printLayerBinding = ((key: Types.layer, value)) => {
         Js.log(key.name);
         StringMap.bindings(value) |> List.iter(printStringBinding)
       };
       Layer.LayerMap.bindings(assignments) |> List.iter(printLayerBinding); */
    /* let cond = Logic.conditionallyAssignedIdentifiers(logic);
       cond |> Logic.IdentifierSet.elements |> List.iter(((ltype, path)) => Js.log(path)); */
    let filterParameters = ((name, _)) =>
      ! Js.String.includes("margin", name)
      && ! Js.String.includes("padding", name);
    let conditionallyAssigned = Logic.conditionallyAssignedIdentifiers(logic);
    let filterConditionallyAssigned = (layer: Types.layer, (name, _)) => {
      let isAssigned = ((_, value)) => value == ["layers", layer.name, name];
      conditionallyAssigned |> Logic.IdentifierSet.exists(isAssigned);
    };
    let defineInitialLayerValues = ((layer, propertyMap)) =>
      propertyMap
      |> StringMap.bindings
      |> List.filter(filterParameters)
      |> List.filter(filterConditionallyAssigned(layer))
      |> List.map(defineInitialLayerValue(layer));
    FunctionDeclaration({
      "name": "update",
      "modifiers": [AccessLevelModifier(PrivateModifier)],
      "parameters": [],
      "result": None,
      "body":
        (
          assignments
          |> Layer.LayerMap.bindings
          |> List.map(defineInitialLayerValues)
          |> List.concat
        )
        @ SwiftLogic.toSwiftAST(
            swiftOptions,
            colors,
            textStyles,
            rootLayer,
            logic
          )
    });
  };
  let textLayers =
    nonRootLayers
    |> List.filter((layer: Types.layer) => layer.typeName == Types.Text);
  TopLevelDeclaration({
    "statements": [
      SwiftDocument.importFramework(swiftOptions.framework),
      ImportDeclaration("Foundation"),
      Empty,
      LineComment("MARK: - " ++ name),
      Empty,
      ClassDeclaration({
        "name": name,
        "inherits": [TypeName(Types.View |> getLayerTypeName)],
        "modifier": Some(PublicModifier),
        "isFinal": false,
        "body":
          Document.joinGroups(
            Empty,
            [
              [Empty, LineComment("MARK: Lifecycle")],
              [initializerDoc()],
              [initializerCoderDoc()],
              List.length(parameters) > 0 ? [LineComment("MARK: Public")] : [],
              parameters |> List.map(parameterVariableDoc),
              [LineComment("MARK: Private")],
              nonRootLayers |> List.map(viewVariableDoc),
              textLayers |> List.map(textStyleVariableDoc),
              rootLayer |> Layer.flatmap(spacingVariableDoc) |> List.concat,
              constraints
              |> List.map(def => constraintVariableDoc(def.variableName)),
              [setUpViewsDoc(rootLayer)],
              [setUpConstraintsDoc(rootLayer)],
              [updateDoc()]
            ]
          )
      }),
      Empty
    ]
  });
};
