open Node;

[@bs.val] [@bs.module "fs-extra"] external ensureDirSync : string => unit = "";

[@bs.val] [@bs.module "fs-extra"]
external copySync : (string, string) => unit = "";

[@bs.module] external getStdin : unit => Js_promise.t(string) = "get-stdin";

let arguments = Array.to_list(Process.argv);

let positionalArguments =
  arguments |> List.filter(arg => ! Js.String.startsWith("--", arg));

let getArgument = name => {
  let prefix = "--" ++ name ++ "=";
  switch (arguments |> List.find(Js.String.startsWith(prefix))) {
  | value =>
    Some(value |> Js.String.sliceToEnd(~from=Js.String.length(prefix)))
  | exception Not_found => None
  };
};

let options: LonaCompilerCore.Options.options = {
  preset:
    switch (getArgument("preset")) {
    | Some("airbnb") => Airbnb
    | _ => Standard
    }
};

let swiftOptions: Swift.Options.options = {
  framework:
    switch (getArgument("framework")) {
    | Some("appkit") => Swift.Options.AppKit
    | _ => Swift.Options.UIKit
    }
};

let exit = message => {
  Js.log(message);
  [%bs.raw {|process.exit(1)|}];
};

if (List.length(positionalArguments) < 3) {
  exit("No command given");
};

let command = List.nth(positionalArguments, 2);

if (List.length(positionalArguments) < 4) {
  exit("No target given");
};

let target =
  switch (List.nth(positionalArguments, 3)) {
  | "js" => Types.JavaScript
  | "swift" => Types.Swift
  | "xml" => Types.Xml
  | _ => exit("Unrecognized target")
  };

/* Rudimentary workspace detection */
let rec findWorkspaceDirectory = path => {
  let exists = Fs.existsSync(Path.join([|path, "colors.json"|]));
  exists ?
    Some(path) :
    (
      switch (Path.dirname(path)) {
      | "/" => None
      | parent => findWorkspaceDirectory(parent)
      }
    );
};

let concat = (base, addition) => Path.join([|base, addition|]);

let getTargetExtension =
  fun
  | Types.JavaScript => ".js"
  | Swift => ".swift"
  | Xml => ".xml";

let targetExtension = getTargetExtension(target);

let renderColors = (target, colors) =>
  switch target {
  | Types.Swift => Swift.Color.render(options, swiftOptions, colors)
  | JavaScript => JavaScript.Color.render(colors)
  | Xml => Xml.Color.render(colors)
  };

let renderTextStyles = (target, colors, textStyles) =>
  switch target {
  | Types.Swift => Swift.TextStyle.render(swiftOptions, colors, textStyles)
  | _ => ""
  };

let convertColors = (target, contents) =>
  Color.parseFile(contents) |> renderColors(target);

let convertTextStyles = (target, filename) =>
  switch (findWorkspaceDirectory(filename)) {
  | None =>
    exit(
      "Couldn't find workspace directory. Try specifying it as a parameter (TODO)"
    )
  | Some(workspace) =>
    let colorsFile =
      Node.Fs.readFileSync(Path.join([|workspace, "colors.json"|]), `utf8);
    let colors = Color.parseFile(colorsFile);
    let textStylesFile = Node.Fs.readFileSync(filename, `utf8);
    TextStyle.parseFile(textStylesFile) |> renderTextStyles(target, colors);
  };

let convertComponent = filename => {
  let contents = Fs.readFileSync(filename, `utf8);
  let parsed = contents |> Js.Json.parseExn;
  let name = Path.basenameExt(~path=filename, ~ext=".component");
  switch target {
  | Types.JavaScript =>
    JavaScript.Component.generate(name, parsed) |> JavaScript.Render.toString
  | Swift =>
    switch (findWorkspaceDirectory(filename)) {
    | None =>
      exit(
        "Couldn't find workspace directory. Try specifying it as a parameter (TODO)"
      )
    | Some(workspace) =>
      let colorsFile =
        Node.Fs.readFileSync(Path.join([|workspace, "colors.json"|]), `utf8);
      let colors = Color.parseFile(colorsFile);
      let textStylesFile =
        Node.Fs.readFileSync(
          Path.join([|workspace, "textStyles.json"|]),
          `utf8
        );
      let textStyles = TextStyle.parseFile(textStylesFile);
      let result =
        Swift.Component.generate(
          options,
          swiftOptions,
          name,
          colors,
          textStyles,
          parsed
        );
      result |> Swift.Render.toString;
    }
  | _ => exit("Unrecognized target")
  };
};

let copyStaticFiles = outputDirectory =>
  switch target {
  | Types.Swift =>
    let framework =
      switch swiftOptions.framework {
      | AppKit => "appkit"
      | UIKit => "uikit"
      };
    copySync(
      concat(
        NodeGlobal.__dirname,
        "../static/swift/AttributedFont." ++ framework ++ ".swift"
      ),
      concat(outputDirectory, "AttributedFont.swift")
    );
  | _ => ()
  };

let findContentsAbove = contents => {
  let lines = contents |> Js.String.split("\n");
  let index =
    lines
    |> Js.Array.findIndex(line =>
         line |> Js.String.includes("LONA: KEEP ABOVE")
       );
  switch index {
  | (-1) => None
  | _ =>
    Some(
      (
        lines
        |> Js.Array.slice(~start=0, ~end_=index + 1)
        |> Js.Array.joinWith("\n")
      ) ++ "\n\n"
    )
  };
};

let findContentsBelow = contents => {
  let lines = contents |> Js.String.split("\n");
  let index =
    lines
    |> Js.Array.findIndex(line =>
         line |> Js.String.includes("LONA: KEEP BELOW")
       );
  switch index {
  | (-1) => None
  | _ =>
    Some(
      "\n" ++ (lines |> Js.Array.sliceFrom(index) |> Js.Array.joinWith("\n"))
    )
  };
};

let convertWorkspace = (workspace, output) => {
  let fromDirectory = Path.resolve([|workspace|]);
  let toDirectory = Path.resolve([|output|]);
  ensureDirSync(toDirectory);
  let colorsInputPath = concat(fromDirectory, "colors.json");
  let colorsOutputPath = concat(toDirectory, "Colors" ++ targetExtension);
  let colors = Color.parseFile(Node.Fs.readFileSync(colorsInputPath, `utf8));
  Fs.writeFileSync(
    ~filename=colorsOutputPath,
    ~text=colors |> renderColors(target)
  );
  let textStylesInputPath = concat(fromDirectory, "textStyles.json");
  let textStylesOutputPath =
    concat(toDirectory, "TextStyles" ++ targetExtension);
  let textStylesFile = Node.Fs.readFileSync(textStylesInputPath, `utf8);
  let textStyles =
    TextStyle.parseFile(textStylesFile) |> renderTextStyles(target, colors);
  Fs.writeFileSync(~filename=textStylesOutputPath, ~text=textStyles);
  copyStaticFiles(toDirectory);
  Glob.glob(
    concat(fromDirectory, "**/*.component"),
    (_, files) => {
      let files = Array.to_list(files);
      let processFile = file => {
        let fromRelativePath =
          Path.relative(~from=fromDirectory, ~to_=file, ());
        let toRelativePath =
          concat(
            Path.dirname(fromRelativePath),
            Path.basenameExt(~path=fromRelativePath, ~ext=".component")
          )
          ++ targetExtension;
        let outputPath = Path.join([|toDirectory, toRelativePath|]);
        Js.log(
          Path.join([|workspace, fromRelativePath|])
          ++ "=>"
          ++ Path.join([|output, toRelativePath|])
        );
        switch (convertComponent(file)) {
        | exception (Json_decode.DecodeError(reason)) =>
          Js.log("Failed to decode " ++ file);
          Js.log(reason);
        | exception (Decode.UnknownParameter(name)) =>
          Js.log("Unknown parameter: " ++ name)
        | contents =>
          ensureDirSync(Path.dirname(outputPath));
          let (contentsAbove, contentsBelow) =
            switch (Fs.readFileAsUtf8Sync(outputPath)) {
            | existing => (findContentsAbove(existing), findContentsBelow(existing))
            | exception _ => (None, None)
            };
          let contents =
            switch contentsAbove {
            | Some(contentsAbove) => contentsAbove ++ contents
            | None => contents
            };
          let contents =
            switch contentsBelow {
            | Some(contentsBelow) => contents ++ contentsBelow
            | None => contents
            };
          Fs.writeFileSync(~filename=outputPath, ~text=contents);
        };
      };
      files |> List.iter(processFile);
    }
  );
  Glob.glob(
    concat(fromDirectory, "**/*.png"),
    (_, files) => {
      let files = Array.to_list(files);
      let processFile = file => {
        let fromRelativePath =
          Path.relative(~from=fromDirectory, ~to_=file, ());
        let outputPath = Path.join([|toDirectory, fromRelativePath|]);
        Js.log(
          Path.join([|workspace, fromRelativePath|])
          ++ "=>"
          ++ Path.join([|output, fromRelativePath|])
        );
        copySync(file, outputPath);
      };
      files |> List.iter(processFile);
    }
  );
};

switch command {
| "workspace" =>
  if (List.length(positionalArguments) < 5) {
    exit("No workspace path given");
  };
  if (List.length(positionalArguments) < 6) {
    exit("No output path given");
  };
  convertWorkspace(
    List.nth(positionalArguments, 4),
    List.nth(positionalArguments, 5)
  );
| "component" =>
  if (List.length(positionalArguments) < 5) {
    exit("No filename given");
  };
  convertComponent(List.nth(positionalArguments, 4)) |> Js.log;
| "colors" =>
  if (List.length(positionalArguments) < 5) {
    let render = contents =>
      Js.Promise.resolve(convertColors(target, contents) |> Js.log);
    getStdin() |> Js.Promise.then_(render) |> ignore;
  } else {
    let contents =
      Node.Fs.readFileSync(List.nth(positionalArguments, 4), `utf8);
    convertColors(target, contents) |> Js.log;
  }
| _ => Js.log2("Invalid command", command)
};