<?xml version='1.0'?> <!--*- mode: xml -*-->

<xsl:stylesheet
  xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
  xmlns:exsl="http://exslt.org/common"
  extension-element-prefixes="exsl"
  version="1.0">
<xsl:output method="xml" indent="yes"
            doctype-public ="-//OASIS//DTD DocBook XML V4.1.2//EN"
            doctype-system = "http://www.oasis-open.org/docbook/xml/4.1.2/docbookx.dtd"/> 

<xsl:param name="module" />

  <xsl:template match="element">
    <xsl:element name="varlistentry">
      <xsl:element name="term">
        <xsl:element name="link">
          <xsl:attribute name="linkend"><xsl:value-of select="$module" />-plugins-<xsl:value-of select="name"/></xsl:attribute>
          <xsl:value-of select="name" />
        </xsl:element>
      </xsl:element>
      <xsl:element name="listitem">
        <xsl:element name="simpara"><xsl:value-of select="description" /></xsl:element>
      </xsl:element>
    </xsl:element>
    <xsl:variable name="name"><xsl:copy-of select="name"/></xsl:variable>
    <!-- here we write an element-(name)-details.xml file for the element -->
    <exsl:document href="{concat ('xml/element-', $name, '-details.xml')}" method="xml" indent="yes">

      <xsl:element name="refsect2">
        <xsl:element name="title">Element Information</xsl:element>
        <xsl:element name="variablelist">
        
          <!-- plugin name and link -->
          <xsl:element name="varlistentry">
            <xsl:element name="term">plugin</xsl:element>
            <xsl:element name="listitem">
              <xsl:element name="simpara">
                <xsl:element name="link">
                  <xsl:attribute name="linkend">plugin-<xsl:value-of select="../../name"/></xsl:attribute>
                  <xsl:value-of select="../../name" />
                </xsl:element>
              </xsl:element>
            </xsl:element>
          </xsl:element>
        
          <xsl:element name="varlistentry">
            <xsl:element name="term">author</xsl:element>
            <xsl:element name="listitem">
              <xsl:element name="simpara"><xsl:value-of select="author" /></xsl:element>
            </xsl:element>
          </xsl:element>
        
          <xsl:element name="varlistentry">
            <xsl:element name="term">class</xsl:element>
            <xsl:element name="listitem">
              <xsl:element name="simpara"><xsl:value-of select="class" /></xsl:element>
            </xsl:element>
          </xsl:element>
                      
        </xsl:element> <!-- variablelist -->

        <xsl:element name="title">Element Pads</xsl:element>
        <!-- process all caps -->
        <xsl:for-each select="pads/caps">
          <xsl:element name="variablelist">
            <xsl:element name="varlistentry">
              <xsl:element name="term">name</xsl:element>
              <xsl:element name="listitem">
                <xsl:element name="simpara"><xsl:value-of select="name" /></xsl:element>
              </xsl:element>
            </xsl:element>
            
            <xsl:element name="varlistentry">
              <xsl:element name="term">direction</xsl:element>
              <xsl:element name="listitem">
                <xsl:element name="simpara"><xsl:value-of select="direction" /></xsl:element>
              </xsl:element>
            </xsl:element>
            
            <xsl:element name="varlistentry">
              <xsl:element name="term">presence</xsl:element>
              <xsl:element name="listitem">
                <xsl:element name="simpara"><xsl:value-of select="presence" /></xsl:element>
              </xsl:element>
            </xsl:element>
            
            <xsl:element name="varlistentry">
              <xsl:element name="term">details</xsl:element>
              <xsl:element name="listitem">
                <xsl:element name="simpara"><xsl:value-of select="details" /></xsl:element>
              </xsl:element>
            </xsl:element>

          </xsl:element> <!-- variablelist -->

          <!--xsl:element name="programlisting"><xsl:value-of select="details" /></xsl:element-->

        </xsl:for-each>
      </xsl:element>

    </exsl:document>
  </xsl:template>

  <xsl:template match="plugin">
    <xsl:element name="refentry">
      <xsl:attribute name="id"><xsl:value-of select="$module" />-plugins-plugin-<xsl:value-of select="name"/></xsl:attribute>

      <xsl:element name="refmeta">
        <xsl:element name="refentrytitle">
          <xsl:value-of select="name"/>
        </xsl:element>
        <xsl:element name="manvolnum">3</xsl:element>
        <xsl:element name="refmiscinfo">FIXME Library</xsl:element>
      </xsl:element> <!-- refmeta -->

      <xsl:element name="refnamediv">
        <xsl:element name="refname">
          <xsl:element name="anchor">
            <xsl:attribute name="id">plugin-<xsl:value-of select="name"/></xsl:attribute>
            <xsl:value-of select="name"/>
          </xsl:element>
        </xsl:element>
  
        <xsl:element name="refpurpose">
          <xsl:value-of select="description"/>
        </xsl:element>
      </xsl:element>

      <xsl:element name="refsect1">
        <xsl:element name="title">Plugin Information</xsl:element>
        <xsl:element name="variablelist">

          <xsl:element name="varlistentry">
            <xsl:element name="term">filename</xsl:element>
            <xsl:element name="listitem">
              <xsl:element name="simpara"><xsl:value-of select="basename" /></xsl:element>
            </xsl:element>
          </xsl:element>

          <xsl:element name="varlistentry">
            <xsl:element name="term">version</xsl:element>
            <xsl:element name="listitem">
              <xsl:element name="simpara"><xsl:value-of select="version" /></xsl:element>
            </xsl:element>
          </xsl:element>

          <xsl:element name="varlistentry">
            <xsl:element name="term">run-time license</xsl:element>
            <xsl:element name="listitem">
              <xsl:element name="simpara"><xsl:value-of select="license" /></xsl:element>
            </xsl:element>
          </xsl:element>

          <xsl:element name="varlistentry">
            <xsl:element name="term">package</xsl:element>
            <xsl:element name="listitem">
              <xsl:element name="simpara"><xsl:value-of select="package" /></xsl:element>
            </xsl:element>
          </xsl:element>

          <xsl:element name="varlistentry">
            <xsl:element name="term">origin</xsl:element>
            <xsl:element name="listitem">
              <xsl:element name="simpara">
                <!-- only show origin as link if it starts with http -->
                <xsl:choose>
                  <xsl:when test="substring(@href, 1, 4) = 'http'">
                    <xsl:element name="ulink">
                      <xsl:attribute name="url"><xsl:value-of select="origin" /></xsl:attribute>
                      <xsl:value-of select="origin" />
                    </xsl:element>
                  </xsl:when>
                  <xsl:otherwise>
                    <xsl:value-of select="origin" />
                  </xsl:otherwise>
                </xsl:choose>
              </xsl:element>
            </xsl:element>
          </xsl:element>

        </xsl:element>
      </xsl:element>

      <xsl:element name="refsect1">
        <xsl:element name="title">Elements</xsl:element>
        <!-- process all elements -->
        <xsl:element name="variablelist">
          <xsl:apply-templates select="elements"/>
        </xsl:element>
      </xsl:element>

    </xsl:element>

  </xsl:template>

  <!-- ignore -->
  <xsl:template match="gst-plugin-paths" />

</xsl:stylesheet>
