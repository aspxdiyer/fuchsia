// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package codegen

const libraryTmpl = `
{{- define "GenerateLibraryFile" -}}
// Code generated by fidlgen; DO NOT EDIT.

package {{ .Name }}

{{if .Libraries}}
import (
{{- range .Libraries }}
	{{ .Alias }} "{{ .Path }}"
{{- end }}
)
{{end}}

{{if .Consts}}
const (
{{- range $const := .Consts }}
	{{- range .DocComments}}
	//{{ . }}
	{{- end}}
	{{ .Name }} {{ .Type }} = {{ .Value }}
{{- end }}
)
{{end}}

{{ range .Enums -}}
{{/* TODO(fxb/59077): Uncomment type assertion once I1102f244aa5ab4545fab21218c1da90be08604ec has landed. */}}
{{/* var _ {{ $.BindingsAlias }}.Enum = {{ .Name }}(0) */}}
{{ template "EnumDefinition" . }}
{{ end -}}
{{ range .Bits -}}
{{ template "BitsDefinition" . }}
{{ end -}}
{{ range .Structs -}}
{{ template "StructDefinition" . }}
{{ end -}}
{{ range .Unions -}}
{{ template "UnionDefinition" . }}
{{ end -}}
{{ range .Tables -}}
{{ template "TableDefinition" . }}
{{ end -}}
{{ range .Protocols -}}
{{ template "ProtocolDefinition" . }}
{{ end -}}

{{- end -}}
`